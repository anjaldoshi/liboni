#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>

#include "oni.h"
#include "onix.h"
#include "oelogo.h"

// Dump raw device streams to files?
// #define DUMPFILES

// Turn on RT optimization
#define RT

#ifdef DUMPFILES
FILE **dump_files;
#endif

// Windows- and UNIX-specific includes etc
#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "liboni")
#include <stdio.h>
#include <stdlib.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

// Display options
volatile int quit = 0;
volatile int display = 1;
int num_frames_to_display = -1;//5;
int device_filter = -1; //ONIX_LOADTEST; // ONIX_TS4231V1ARR

// Global state
volatile oni_ctx ctx = NULL;
oni_size_t num_devs = 0;
oni_device_t *devices = NULL;
int running = 1;

#ifdef _WIN32
HANDLE read_thread;
HANDLE write_thread;
#else
pthread_t read_tid;
pthread_t write_tid;
#endif

int parse_reg_cmd(const char *cmd, long *values, int len)
{
    char *end;
    int k = 0;
    for (long i = strtol(cmd, &end, 10);
         cmd != end;
         i = strtol(cmd, &end, 10))
    {
        cmd = end;
        if (errno == ERANGE){ return -1; }

        values[k++] = i;
        if (k == 3)
            break;
    }

    if (k < len)
        return -1;

    return 0;
}

// Simple & slow device lookup
int find_dev(oni_dev_idx_t idx)
{
    int i;
    for (i = 0; i < num_devs; i++)
        if (devices[i].idx == idx)
            return i;

    return -1;
}

int16_t last_sample = 32767;
uint32_t out_count = 0;

#ifdef _WIN32
DWORD WINAPI read_loop(LPVOID lpParam)
#else
void *read_loop(void *vargp)
#endif
{
    unsigned long counter = 0;
    unsigned long print_count = 0;
    unsigned long this_cnt = 0;

    //// Pre-allocate write frame
    //oni_frame_t *w_frame = NULL;
    //oni_create_frame(ctx, &w_frame, 8, 4);

    while (!quit)  {

        int rc = 0;
        oni_frame_t *frame = NULL;
        rc = oni_read_frame(ctx, &frame);
        if (rc < 0) {
            printf("Error: %s\n", oni_error_str(rc));
            quit = 1;
            break;
        }

        int i = find_dev(frame->dev_idx);
        if (i == -1) goto next;

#ifdef DUMPFILES
        fwrite(frame->data, 1, frame->data_sz, dump_files[i]);
#endif

        if (display
            && ((num_frames_to_display  <= 0 && counter % 1000 == 0) || (num_frames_to_display > 0 && print_count < num_frames_to_display))
            && (device_filter < 0 || devices[i].id == device_filter)
            ) {
            oni_device_t this_dev = devices[i];

            this_cnt++;
            printf("\t[%llu] Dev: %zu (%s) \n",
                frame->time,
                frame->dev_idx,
                onix_device_str(this_dev.id));
                // this_cnt);

            size_t i;
            printf("\tData: [");
            for (i = 0; i < frame->data_sz; i += 2)
                printf("%u ", *(uint16_t *)(frame->data + i));
            printf("]\n");

            print_count++;
        }

         //// Feedback loop test
         //if (frame->dev_idx == 7) {
         //
         //    int16_t sample = *(int16_t *)(frame->data + 10);
         //
         //    if (sample - last_sample > 500) {
         //
         //        memcpy(w_frame->data, &out_count, 4);
         //        out_count++;

         //        int rc = oni_write_frame(ctx, w_frame);
         //        if (rc < 0) { printf("Error: %s\n", oni_error_str(rc)); }

         //    }
         //
         //    last_sample = sample;
         //}
next:
        counter++;
        oni_destroy_frame(frame);
    }

    //oni_destroy_frame(w_frame);

    return NULL;
}

//#ifdef _WIN32
//DWORD WINAPI write_loop(LPVOID lpParam)
//#else
//void *write_loop(void *vargp)
//#endif
//{
//    // Pre-allocate write frame
//    // TODO: hardcoded dev_idx not good
//    oni_frame_t *w_frame = NULL;
//    int rc = oni_create_frame(ctx, &w_frame, 6, &out_count, sizeof(out_count));
//    if (rc < 0) {
//        printf("Error: %s\n", oni_error_str(rc));
//        goto error;
//    }
//
//    // Loop count
//    // uint32_t count = 0;
//
//    // Cycle through writable devices and write counter to their data
//    while (!quit) {
//
//
//        int rc = oni_write_frame(ctx, w_frame);
//        if (rc < 0) {
//            printf("Error: %s\n", oni_error_str(rc));
//            goto error;
//        }
//
//        memcpy(w_frame->data, &out_count, 4);
//        out_count++;
//
//        // count++;
//
//#ifdef _WIN32
//        Sleep(1);
//#else
//        usleep(1000);
//#endif
//    }
//
//error:
//    oni_destroy_frame(w_frame);
//    return NULL;
//}

void start_threads()
{
    // Generate data read_thread and continue here config/signal handling in parallel
#ifdef _WIN32
    DWORD read_tid;
    read_thread = CreateThread(NULL, 0, read_loop, NULL, 0, &read_tid);

    //DWORD write_tid;
    //write_thread = CreateThread(NULL, 0, write_loop, NULL, 0, &write_tid);

#ifdef RT
    if (!SetThreadPriority(read_thread, THREAD_PRIORITY_TIME_CRITICAL))
        printf("Unable to set read thread priority.\n");
    //if (!SetThreadPriority(write_thread, THREAD_PRIORITY_HIGHEST))
    //    printf("Unable to set read thread priority.\n");
#endif

#else
    pthread_create(&read_tid, NULL, read_loop, NULL);
    //pthread_create(&write_tid, NULL, write_loop, NULL);
#endif
}

void stop_threads()
{
    // Join data and signal threads
    quit = 1;

#ifdef _WIN32

    WaitForSingleObject(read_thread, 200); // INFINITE);
    CloseHandle(read_thread);

    //WaitForSingleObject(write_thread, 200);
    //CloseHandle(write_thread);
#else
    if (running)
        pthread_join(read_tid, NULL);
    //pthread_join(write_tid, NULL);
#endif

    oni_size_t run = 0;
    int rc = oni_set_opt(ctx, ONI_OPT_RUNNING, &run, sizeof(run));
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }
}

void print_dev_table(oni_device_t *devices, int num_devs)
{
    // Show device table
    printf("   +--------------------+-------+-------+-------+-------+---------------------\n");
    printf("   |        \t\t|  \t|Firm.\t|Read\t|Wrt. \t|     \n");
    printf("   |Dev. idx\t\t|ID\t|ver. \t|size\t|size \t|Desc.\n");
    printf("   +--------------------+-------+-------+-------+-------+---------------------\n");

    size_t dev_idx;
    for (dev_idx = 0; dev_idx < num_devs; dev_idx++) {

        const char *dev_str = onix_device_str(devices[dev_idx].id);

        printf("%02zd |%05zd: 0x%02x.0x%02x\t|%d\t|%d\t|%u\t|%u\t|%s\n",
               dev_idx,
               devices[dev_idx].idx,
               (uint8_t)(devices[dev_idx].idx >> 8),
               (uint8_t)devices[dev_idx].idx,
               devices[dev_idx].id,
               devices[dev_idx].version,
               devices[dev_idx].read_size,
               devices[dev_idx].write_size,
               dev_str);
    }

    printf("   +--------------------+-------+-------+-------+-------+---------------------\n");
}

int main(int argc, char *argv[])
{
    printf(oe_logo_med);

    oni_size_t block_read_size = 2048;
    oni_size_t block_write_size = 2048;
    int host_idx = -1;
    char *driver;

    switch (argc) {

        case 5:
            block_write_size = atoi(argv[4]);
        case 4:
            block_read_size = atoi(argv[3]);
        case 3:
            host_idx = atoi(argv[2]);
        case 2:
            driver = argv[1];
            break;
        default:
            printf("Usage: %s <driver> [host_index] [read block bytes] [write block bytes]\n", argv[0]);
            exit(1);
    }

#if defined(_WIN32) && defined(RT)
    if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
        printf("Failed to set thread priority\n");
        exit(EXIT_FAILURE);
    }
#else
    // TODO
#endif

    // Return code
    int rc = ONI_ESUCCESS;

    // Generate context
    ctx = oni_create_ctx(driver);
    if (!ctx) { printf("Failed to create context\n"); exit(EXIT_FAILURE); }

    // Initialize context and discover hardware
    rc = oni_init_ctx(ctx, host_idx);
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }
    assert(rc == 0);

    // Set ONIX_FLAG0 to turn on pass-through and issue reset
    oni_reg_val_t val = 0;
    rc = oni_set_opt(ctx, ONIX_OPT_PASSTHROUGH, &val, sizeof(val));
    rc = oni_set_opt(ctx, ONI_OPT_RESET, &val, sizeof(val));

    // Examine device table
    size_t num_devs_sz = sizeof(num_devs);
    oni_get_opt(ctx, ONI_OPT_NUMDEVICES, &num_devs, &num_devs_sz);

    // Get the device table
    size_t devices_sz = sizeof(oni_device_t) * num_devs;
    devices = (oni_device_t *)realloc(devices, devices_sz);
    if (devices == NULL) { exit(EXIT_FAILURE); }
    oni_get_opt(ctx, ONI_OPT_DEVICETABLE, devices, &devices_sz);

#ifdef DUMPFILES
    // Make room for dump files
    dump_files = malloc(num_devs * sizeof(FILE *));

    // Open dump files
    for (size_t i = 0; i < num_devs; i++) {
        char * buffer = malloc(100);
        snprintf(buffer, 100, "%s_idx-%zd_id-%d.raw", "dev", i, devices[i].id);
        dump_files[i] = fopen(buffer, "wb");
        free(buffer);
    }
#endif

    // Show device table
    print_dev_table(devices, num_devs);

    oni_size_t frame_size = 0;
    size_t frame_size_sz = sizeof(frame_size);
    oni_get_opt(ctx, ONI_OPT_MAXREADFRAMESIZE, &frame_size, &frame_size_sz);
    printf("Max. read frame size: %u bytes\n", frame_size);

    oni_get_opt(ctx, ONI_OPT_MAXWRITEFRAMESIZE, &frame_size, &frame_size_sz);
    printf("Max. write frame size: %u bytes\n", frame_size);

    printf("Setting block read size to: %u bytes\n", block_read_size);
    size_t block_size_sz = sizeof(block_read_size);
    rc = oni_set_opt(ctx, ONI_OPT_BLOCKREADSIZE, &block_read_size, block_size_sz);
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }
    assert(!rc && "Failure to set block read size");

    oni_get_opt(ctx, ONI_OPT_BLOCKREADSIZE, &block_read_size, &block_size_sz);
    printf("Block read size: %u bytes\n", block_read_size);

    printf("Setting write pre-allocation buffer to: %u bytes\n", block_write_size);
    block_size_sz = sizeof(block_write_size);
    rc = oni_set_opt(ctx, ONI_OPT_BLOCKWRITESIZE, &block_write_size, block_size_sz);
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }

    oni_get_opt(ctx, ONI_OPT_BLOCKWRITESIZE, &block_write_size, &block_size_sz);
    printf("Write pre-allocation size: %u bytes\n", block_write_size);

    // Try to write to base clock freq, which is write only
    oni_size_t reg = (oni_size_t)10e6;
    rc = oni_set_opt(ctx, ONI_OPT_SYSCLKHZ, &reg, sizeof(oni_size_t));
    assert(rc == ONI_EREADONLY && "Successful write to read-only register.");

    size_t reg_sz = sizeof(reg);
    rc = oni_get_opt(ctx, ONI_OPT_SYSCLKHZ, &reg, &reg_sz);
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }
    assert(!rc && "Register read failure.");
    printf("System clock rate: %u Hz\n", reg);

    rc = oni_get_opt(ctx, ONI_OPT_ACQCLKHZ, &reg, &reg_sz);
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }
    assert(!rc && "Register read failure.");
    printf("Frame counter clock rate: %u Hz\n", reg);

    reg = 42;
    rc = oni_set_opt(ctx, ONI_OPT_HWADDRESS, &reg, sizeof(oni_size_t));
    assert(!rc && "Register write failure.");

    rc = oni_get_opt(ctx, ONI_OPT_HWADDRESS, &reg, &reg_sz);
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }
    assert(!rc && "Register read failure.");
    printf("Hardware address: 0x%08x\n", reg);

    rc = oni_get_opt(ctx, ONI_OPT_RUNNING, &reg, &reg_sz);
    if (rc) {printf("Error: %s\n", oni_error_str(rc)); }
    assert(!rc && "Register read failure.");
    printf("Hardware run state: %d\n", reg);
    printf("Resetting acquisition clock and starting hardware run simultaneously...\n");

    // Restart acquisition clock counter and start acquisition simultaneously
    reg = 2;
    rc = oni_set_opt(ctx, ONI_OPT_RESETACQCOUNTER, &reg, sizeof(oni_size_t));
    assert(!rc && "Register write failure.");

    rc = oni_get_opt(ctx, ONI_OPT_RUNNING, &reg, &reg_sz);
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }
    assert(!rc && "Register read failure.");
    assert(reg == 1 && "ONI_OPT_RUNNING should be 1.");
    printf("Hardware run state: %d\n", reg);

    //// Start acquisition
    //quit = 0;
    //reg = 1; // Reset clock
    //rc = oni_set_opt(ctx, ONI_OPT_RUNNING, &reg, sizeof(oni_size_t));
    //if (rc) { printf("Error: %s\n", oni_error_str(rc)); }

    // Start reading and writing threads
    start_threads();

    // Read stdin to start (s) or pause (p)
    printf("Some commands can cause hardware malfunction if issued in the wrong order!\n");
    int c = 'x';
    while (c != 'q') {

        printf("Enter a command and press enter:\n");
        printf("\td - toggle 1/1000 display\n");
        printf("\tt - print device table\n");
        printf("\tp - toggle pause register\n");
        printf("\ts - toggle pause register & r/w thread operation\n");
        printf("\tr - read from device register\n");
        printf("\tw - write to device register\n");
        printf("\th - get hub information about a device\n");
        printf("\ta - reset the acquisition clock counter\n");
        printf("\tx - issue a hardware reset\n");
        printf("\tq - quit\n");
        printf(">>> ");

        char *cmd = NULL;
        size_t cmd_len = 0;
        rc = getline(&cmd, &cmd_len, stdin);
        if (rc == -1) { printf("Error: bad command\n"); continue; }
        c = cmd[0];
        free(cmd);

        if (c == 'p') {
            running = (running == 1) ? 0 : 1;
            oni_size_t run = running;
            rc = oni_set_opt(ctx, ONI_OPT_RUNNING, &run, sizeof(run));
            if (rc) {
                printf("Error: %s\n", oni_error_str(rc));
            }
            printf("Paused\n");
        }
        else if (c == 'x') {
            oni_size_t reset = 1;
            rc = oni_set_opt(ctx, ONI_OPT_RESET, &reset, sizeof(reset));
            if (rc) { printf("Error: %s\n", oni_error_str(rc)); }
        }
        else if (c == 'd') {
            display = (display == 0) ? 1 : 0;
        }
        else if (c == 't') {
            print_dev_table(devices, num_devs);
        }
        else if (c == 'w') {
            printf("Write to a device register.\n");
            printf("Enter: dev_idx reg_addr reg_val\n");
            printf(">>> ");

            // Read the command
            char *buf = NULL;
            size_t len = 0;
            rc = getline(&buf, &len, stdin);
            if (rc == -1) { printf("Error: bad command\n"); continue; }

            // Parse the command string
            long values[3];
            rc = parse_reg_cmd(buf, values, 3);
            if (rc == -1) { printf("Error: bad command\n"); continue; }
            free(buf);

            size_t dev_idx = (size_t)values[0];
            oni_size_t addr = (oni_size_t)values[1];
            oni_size_t val = (oni_size_t)values[2];

            rc = oni_write_reg(ctx, dev_idx, addr, val);
            printf("%s\n", oni_error_str(rc));
        }
        else if (c == 'r') {
            printf("Read a device register.\n");
            printf("Enter: dev_idx reg_addr\n");
            printf(">>> ");

            // Read the command
            char *buf = NULL;
            size_t len = 0;
            rc = getline(&buf, &len, stdin);
            if (rc == -1) { printf("Error: bad command\n"); continue; }

            // Parse the command string
            long values[2];
            rc = parse_reg_cmd(buf, values, 2);
            if (rc == -1) { printf("Error: bad command\n"); continue; }
            free(buf);

            size_t dev_idx = (size_t)values[0];
            oni_size_t addr = (oni_size_t)values[1];

            oni_reg_val_t val = 0;
            rc = oni_read_reg(ctx, dev_idx, addr, &val);
            if (!rc) {
                printf("Reg. value: %u\n", val);
            } else {
                printf("%s\n", oni_error_str(rc));
            }
        } else if (c == 'h') {
            printf("Get information about the hub for a given device.\n");
            printf("Enter: dev_idx\n");
            printf(">>> ");

            // Read the command
            char *buf = NULL;
            size_t len = 0;
            rc = getline(&buf, &len, stdin);
            if (rc == -1) {
                printf("Error: bad command\n");
                continue;
            }

            // Parse the command string
            long values[1];
            rc = parse_reg_cmd(buf, values, 1);
            if (rc == -1) {
                printf("Error: bad command\n");
                continue;
            }
            free(buf);

            size_t hub_idx = (size_t)values[0] & 0x0000FF00;

            oni_reg_val_t hub_hw_id = 0;
            rc = oni_read_reg(ctx,
                              hub_idx + ONIX_HUB_DEV_IDX,
                              ONIX_HUB_HARDWAREID,
                              &hub_hw_id);
            printf("Hub hardware ID: ");
            rc ? printf("%s\n", oni_error_str(rc)) :
                 printf("%u, %s\n", hub_hw_id, onix_hub_str(hub_hw_id));

            oni_reg_val_t hub_hw_rev = 0;
            rc = oni_read_reg(ctx,
                              hub_idx + ONIX_HUB_DEV_IDX,
                              ONIX_HUB_HARDWAREREV,
                              &hub_hw_rev);
            printf("Hub hardware revision: ");
            rc ? printf("%s\n", oni_error_str(rc)) :
                 printf("%u.%u\n", (hub_hw_rev & 0xFF00) >> 8, hub_hw_rev & 0xFF);

            oni_reg_val_t hub_firm_ver = 0;
            rc = oni_read_reg(ctx,
                              hub_idx + ONIX_HUB_DEV_IDX,
                              ONIX_HUB_FIRMWAREVER,
                              &hub_firm_ver);
            printf("Hub firmware version: ");
            rc ? printf("%s\n", oni_error_str(rc)) :
                 printf("%u.%u\n",(hub_firm_ver & 0xFF00) >> 8, hub_firm_ver & 0xFF);

            oni_reg_val_t hub_clk_hz = 0;
            rc = oni_read_reg(ctx,
                              hub_idx + ONIX_HUB_DEV_IDX,
                              ONIX_HUB_CLKRATEHZ,
                              &hub_clk_hz);
            printf("Hub clock frequency (Hz): ");
            rc ? printf("%s\n", oni_error_str(rc)) : printf("%u\n", hub_clk_hz);

            oni_reg_val_t hub_delay_ns = 0;
            rc = oni_read_reg(ctx,
                              hub_idx + ONIX_HUB_DEV_IDX,
                              ONIX_HUB_DELAYNS,
                              &hub_delay_ns);
            printf("Hub transmission delay (ns): ");
            rc ? printf("%s\n", oni_error_str(rc)) : printf("%u\n", hub_delay_ns);

        }
        else if (c == 's') {

            if (quit == 0) {
                stop_threads();
            }
            else {
                start_threads();
            }
        }
        else if (c == 'a') {
            reg = 1;
            rc = oni_set_opt(ctx, ONI_OPT_RESETACQCOUNTER, &reg, sizeof(oni_size_t));
            assert(!rc && "Register write failure.");
            printf("Acquisition clock counter reset issued.\n");
        }
    }

    stop_threads();

#ifdef DUMPFILES
    // Close dump files
    for (int dev_idx = 0; dev_idx < num_devs; dev_idx++) {
        fclose(dump_files[dev_idx]);
    }
#endif

    // Stop hardware
    oni_size_t run = 0 ;
    rc = oni_set_opt(ctx, ONI_OPT_RUNNING, &run, sizeof(run));
    if (rc) { printf("Error: %s\n", oni_error_str(rc)); }

    // Free dynamic stuff
    oni_destroy_ctx(ctx);
    free(devices);

    return 0;
}
