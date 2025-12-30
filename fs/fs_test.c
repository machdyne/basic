/*
 * Example usage and test program for the F-RAM filesystem
 */

#include <stdio.h>
#include <string.h>
#include "fs.h"

/* Mock F-RAM storage for testing */
static uint8_t mock_fram[8 * 1024 * 1024];

uint8_t fram_read(int addr) {
    return mock_fram[addr];
}

void fram_write(int addr, unsigned char d) {
    mock_fram[addr] = d;
}

void fram_write_enable(void) {
    /* Nothing needed for mock */
}

/* Test the filesystem */
int main(void) {
    printf("=== F-RAM Filesystem Test ===\n\n");
    
    /* Initialize filesystem */
    printf("Initializing filesystem...\n");
    fs_init();
    
    /* List files (should be empty) */
    printf("\nInitial state:\n");
    hw_list();
    
    /* Save some test files */
    printf("\n--- Saving test files ---\n");
    
    uint8_t data1[] = "HELLO WORLD";
    int ret = hw_save("HELLO.BAS", data1, sizeof(data1) - 1);
    printf("Save HELLO.BAS: %s\n", ret == FS_OK ? "OK" : "FAILED");
    
    uint8_t data2[] = "10 PRINT \"TEST\"\n20 GOTO 10";
    ret = hw_save("TEST.BAS", data2, sizeof(data2) - 1);
    printf("Save TEST.BAS: %s\n", ret == FS_OK ? "OK" : "FAILED");
    
    uint8_t data3[1000];
    for (int i = 0; i < 1000; i++) {
        data3[i] = i & 0xFF;
    }
    ret = hw_save("BINARY.DAT", data3, 1000);
    printf("Save BINARY.DAT: %s\n", ret == FS_OK ? "OK" : "FAILED");
    
    /* List files */
    printf("\nAfter saving:\n");
    hw_list();
    
    /* Load and verify */
    printf("\n--- Loading files ---\n");
    
    uint8_t buffer[2000];
    uint16_t len;
    
    ret = hw_load("HELLO.BAS", buffer, &len, sizeof(buffer));
    if (ret == FS_OK) {
        buffer[len] = '\0';
        printf("Loaded HELLO.BAS (%d bytes): %s\n", len, buffer);
    } else {
        printf("Failed to load HELLO.BAS: %d\n", ret);
    }
    
    ret = hw_load("TEST.BAS", buffer, &len, sizeof(buffer));
    if (ret == FS_OK) {
        buffer[len] = '\0';
        printf("Loaded TEST.BAS (%d bytes):\n%s\n", len, buffer);
    } else {
        printf("Failed to load TEST.BAS: %d\n", ret);
    }
    
    ret = hw_load("BINARY.DAT", buffer, &len, sizeof(buffer));
    if (ret == FS_OK) {
        printf("Loaded BINARY.DAT (%d bytes)\n", len);
        /* Verify binary data */
        int ok = 1;
        for (int i = 0; i < len; i++) {
            if (buffer[i] != (i & 0xFF)) {
                ok = 0;
                break;
            }
        }
        printf("Binary data verification: %s\n", ok ? "PASS" : "FAIL");
    } else {
        printf("Failed to load BINARY.DAT: %d\n", ret);
    }
    
    /* Test overwrite */
    printf("\n--- Testing overwrite ---\n");
    uint8_t data4[] = "OVERWRITTEN!";
    ret = hw_save("HELLO.BAS", data4, sizeof(data4) - 1);
    printf("Overwrite HELLO.BAS: %s\n", ret == FS_OK ? "OK" : "FAILED");
    
    ret = hw_load("HELLO.BAS", buffer, &len, sizeof(buffer));
    if (ret == FS_OK) {
        buffer[len] = '\0';
        printf("Loaded HELLO.BAS (%d bytes): %s\n", len, buffer);
    }
    
    /* Test delete */
    printf("\n--- Testing delete ---\n");
    ret = hw_delete("TEST.BAS");
    printf("Delete TEST.BAS: %s\n", ret == FS_OK ? "OK" : "FAILED");
    
    printf("\nAfter delete:\n");
    hw_list();
    
    /* Test file not found */
    printf("\n--- Testing error cases ---\n");
    ret = hw_load("NOTEXIST.BAS", buffer, &len, sizeof(buffer));
    printf("Load non-existent file: %s\n", 
           ret == FS_ERR_NOT_FOUND ? "Correctly returned NOT_FOUND" : "ERROR");
    
    ret = hw_delete("NOTEXIST.BAS");
    printf("Delete non-existent file: %s\n",
           ret == FS_ERR_NOT_FOUND ? "Correctly returned NOT_FOUND" : "ERROR");
    
    /* Test filesystem check */
    printf("\n--- Checking filesystem ---\n");
    fs_check();
    
    /* Save many small files */
    printf("\n--- Testing many small files ---\n");
    for (int i = 0; i < 10; i++) {
        char filename[20];
        sprintf(filename, "FILE%d.TXT", i);
        uint8_t small_data[10];
        sprintf((char*)small_data, "DATA%d", i);
        hw_save(filename, small_data, strlen((char*)small_data));
    }
    
    printf("\nAfter adding many files:\n");
    hw_list();
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
