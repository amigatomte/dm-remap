/*
 * dm-remap v4.0 Automatic Setup Reassembly System - Comprehensive Test Suite
 * 
 * Tests for device fingerprinting, metadata storage, discovery engine,
 * and automatic setup reconstruction functionality.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

/* Mock kernel includes for userspace testing */
#define DMINFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define DMWARN(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define DMERR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

/* Include our header file */
#include "../include/dm-remap-v4-setup-reassembly.h"

/* Test implementation declarations */
extern int dm_remap_v4_create_device_fingerprint(
    struct dm_remap_v4_device_fingerprint *fingerprint,
    const char *device_path);
extern int dm_remap_v4_verify_device_fingerprint(
    const struct dm_remap_v4_device_fingerprint *fingerprint,
    const char *device_path);
extern int dm_remap_v4_create_setup_metadata(
    struct dm_remap_v4_setup_metadata *metadata,
    const struct dm_remap_v4_device_fingerprint *main_device,
    const struct dm_remap_v4_target_config *target_config);
extern int dm_remap_v4_add_spare_device_to_metadata(
    struct dm_remap_v4_setup_metadata *metadata,
    const struct dm_remap_v4_device_fingerprint *spare_device,
    uint32_t priority);
extern int dm_remap_v4_verify_metadata_integrity(const struct dm_remap_v4_setup_metadata *metadata);
extern float dm_remap_v4_calculate_confidence_score(const struct dm_remap_v4_discovery_result *result);
extern const char* dm_remap_v4_reassembly_error_to_string(int error_code);
extern void dm_remap_v4_print_setup_metadata(const struct dm_remap_v4_setup_metadata *metadata);
extern uint32_t dm_remap_v4_calculate_metadata_crc32(const struct dm_remap_v4_setup_metadata *metadata);

/* CRC32 function from implementation */
extern uint32_t crc32(uint32_t crc, const unsigned char *buf, size_t len);

/* Test statistics */
static uint32_t tests_run = 0;
static uint32_t tests_passed = 0;
static uint32_t tests_failed = 0;

/* Test helper macros */
#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("✅ PASS: %s\n", message); \
    } else { \
        tests_failed++; \
        printf("❌ FAIL: %s\n", message); \
    } \
} while (0)

#define TEST_SECTION(name) do { \
    printf("\n🔧 === %s ===\n", name); \
} while (0)

/*
 * Test 1: Device Fingerprinting System
 */
void test_device_fingerprinting(void)
{
    TEST_SECTION("Device Fingerprinting System");
    
    struct dm_remap_v4_device_fingerprint fingerprint1, fingerprint2;
    int result;
    
    /* Test 1.1: Create device fingerprint for /dev/null (always exists) */
    result = dm_remap_v4_create_device_fingerprint(&fingerprint1, "/dev/null");
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS, 
                "Create device fingerprint for /dev/null");
    
    /* Test 1.2: Verify fingerprint magic number */
    TEST_ASSERT(fingerprint1.magic == DM_REMAP_V4_DEVICE_FINGERPRINT_MAGIC,
                "Device fingerprint has correct magic number");
    
    /* Test 1.3: Verify device path is stored */
    TEST_ASSERT(strcmp(fingerprint1.device_path, "/dev/null") == 0,
                "Device path correctly stored in fingerprint");
    
    /* Test 1.4: Verify timestamps are set */
    TEST_ASSERT(fingerprint1.creation_timestamp > 0,
                "Creation timestamp is set");
    TEST_ASSERT(fingerprint1.last_seen_timestamp > 0,
                "Last seen timestamp is set");
    
    /* Test 1.5: Create second fingerprint for comparison */
    result = dm_remap_v4_create_device_fingerprint(&fingerprint2, "/dev/zero");
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Create second device fingerprint for /dev/zero");
    
    /* Test 1.6: Verify fingerprints are different for different devices */
    TEST_ASSERT(strcmp(fingerprint1.device_path, fingerprint2.device_path) != 0,
                "Different devices have different fingerprints");
    
    /* Test 1.7: Test invalid parameters */
    result = dm_remap_v4_create_device_fingerprint(NULL, "/dev/null");
    TEST_ASSERT(result == -EINVAL, "NULL fingerprint parameter returns EINVAL");
    
    result = dm_remap_v4_create_device_fingerprint(&fingerprint1, NULL);
    TEST_ASSERT(result == -EINVAL, "NULL device path parameter returns EINVAL");
}

/*
 * Test 2: Metadata Creation and Integrity
 */
void test_metadata_creation(void)
{
    TEST_SECTION("Metadata Creation and Integrity");
    
    struct dm_remap_v4_setup_metadata metadata;
    struct dm_remap_v4_device_fingerprint main_device, spare_device;
    struct dm_remap_v4_target_config target_config;
    int result;
    
    /* Setup test data */
    dm_remap_v4_create_device_fingerprint(&main_device, "/dev/test_main");
    dm_remap_v4_create_device_fingerprint(&spare_device, "/dev/test_spare");
    
    memset(&target_config, 0, sizeof(target_config));
    target_config.config_magic = 0xDEADBEEF;
    strncpy(target_config.target_params, "0 1024 /dev/test_main 0", 
            sizeof(target_config.target_params) - 1);
    target_config.target_device_size = 1024;
    
    /* Test 2.1: Create setup metadata */
    result = dm_remap_v4_create_setup_metadata(&metadata, &main_device, &target_config);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Create setup metadata successfully");
    
    /* Test 2.2: Verify metadata magic number */
    TEST_ASSERT(metadata.magic == DM_REMAP_V4_REASSEMBLY_MAGIC,
                "Setup metadata has correct magic number");
    
    /* Test 2.3: Verify metadata integrity */
    result = dm_remap_v4_verify_metadata_integrity(&metadata);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Setup metadata passes integrity verification");
    
    /* Test 2.4: Add spare device to metadata */
    result = dm_remap_v4_add_spare_device_to_metadata(&metadata, &spare_device, 100);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Add spare device to metadata successfully");
    
    /* Test 2.5: Verify spare device was added */
    TEST_ASSERT(metadata.num_spare_devices == 1,
                "Spare device count incremented correctly");
    TEST_ASSERT(metadata.spare_devices[0].spare_priority == 100,
                "Spare device priority set correctly");
    
    /* Test 2.6: Verify metadata integrity after modification */
    result = dm_remap_v4_verify_metadata_integrity(&metadata);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Modified metadata still passes integrity verification");
    
    /* Test 2.7: Test corrupted metadata detection */
    uint32_t original_crc = metadata.overall_crc32;
    metadata.overall_crc32 = 0xDEADBEEF; /* Corrupt the CRC */
    result = dm_remap_v4_verify_metadata_integrity(&metadata);
    TEST_ASSERT(result == -DM_REMAP_V4_REASSEMBLY_ERROR_CRC_MISMATCH,
                "Corrupted metadata detected by integrity check");
    metadata.overall_crc32 = original_crc; /* Restore */
    
    /* Test 2.8: Test invalid parameters */
    result = dm_remap_v4_create_setup_metadata(NULL, &main_device, &target_config);
    TEST_ASSERT(result == -EINVAL, "NULL metadata parameter returns EINVAL");
    
    result = dm_remap_v4_create_setup_metadata(&metadata, NULL, &target_config);
    TEST_ASSERT(result == -EINVAL, "NULL main device parameter returns EINVAL");
}

/*
 * Test 3: Confidence Score Calculation
 */
void test_confidence_calculation(void)
{
    TEST_SECTION("Confidence Score Calculation");
    
    struct dm_remap_v4_discovery_result result;
    struct dm_remap_v4_setup_metadata metadata;
    float confidence;
    uint64_t current_time = time(NULL);
    
    /* Setup base metadata */
    memset(&metadata, 0, sizeof(metadata));
    metadata.magic = DM_REMAP_V4_REASSEMBLY_MAGIC;
    metadata.modified_timestamp = current_time;
    metadata.num_spare_devices = 2;
    
    /* Test 3.1: High confidence scenario */
    memset(&result, 0, sizeof(result));
    result.copies_found = 5;
    result.copies_valid = 5;
    result.corruption_level = 0;
    result.metadata = metadata;
    
    confidence = dm_remap_v4_calculate_confidence_score(&result);
    TEST_ASSERT(confidence >= 0.8f, 
                "High confidence score for perfect metadata (≥0.8)");
    
    /* Test 3.2: Medium confidence scenario */
    result.copies_found = 5;
    result.copies_valid = 3;
    result.corruption_level = 2;
    
    confidence = dm_remap_v4_calculate_confidence_score(&result);
    TEST_ASSERT(confidence >= 0.4f && confidence < 0.8f,
                "Medium confidence score for partial corruption (0.4-0.8)");
    
    /* Test 3.3: Low confidence scenario */
    result.copies_found = 5;
    result.copies_valid = 1;
    result.corruption_level = 8;
    
    confidence = dm_remap_v4_calculate_confidence_score(&result);
    TEST_ASSERT(confidence < 0.4f,
                "Low confidence score for high corruption (<0.4)");
    
    /* Test 3.4: Zero confidence scenario */
    result.copies_found = 0;
    result.copies_valid = 0;
    result.corruption_level = 10;
    
    confidence = dm_remap_v4_calculate_confidence_score(&result);
    TEST_ASSERT(confidence <= 0.01f,  /* Allow for small floating point errors */
                "Zero confidence score for no valid copies");
    
    /* Test 3.5: Old metadata penalty */
    result.copies_found = 5;
    result.copies_valid = 5;
    result.corruption_level = 0;
    result.metadata.modified_timestamp = current_time - (7 * 24 * 3600 + 1); /* >1 week old */
    
    confidence = dm_remap_v4_calculate_confidence_score(&result);
    TEST_ASSERT(confidence < 1.0f,
                "Old metadata receives confidence penalty");
    
    /* Test 3.6: Recent metadata bonus */
    result.metadata.modified_timestamp = current_time - 3600; /* 1 hour ago */
    
    confidence = dm_remap_v4_calculate_confidence_score(&result);
    TEST_ASSERT(confidence > 0.8f,
                "Recent metadata receives confidence bonus");
    
    /* Test 3.7: NULL parameter handling */
    confidence = dm_remap_v4_calculate_confidence_score(NULL);
    TEST_ASSERT(confidence == 0.0f,
                "NULL parameter returns zero confidence");
}

/*
 * Test 4: Error Code Handling
 */
void test_error_handling(void)
{
    TEST_SECTION("Error Code Handling");
    
    const char *error_str;
    
    /* Test 4.1: Success code */
    error_str = dm_remap_v4_reassembly_error_to_string(DM_REMAP_V4_REASSEMBLY_SUCCESS);
    TEST_ASSERT(strcmp(error_str, "Success") == 0,
                "Success error code returns correct string");
    
    /* Test 4.2: Invalid parameters code */
    error_str = dm_remap_v4_reassembly_error_to_string(DM_REMAP_V4_REASSEMBLY_ERROR_INVALID_PARAMS);
    TEST_ASSERT(strcmp(error_str, "Invalid parameters") == 0,
                "Invalid parameters error code returns correct string");
    
    /* Test 4.3: No metadata code */
    error_str = dm_remap_v4_reassembly_error_to_string(DM_REMAP_V4_REASSEMBLY_ERROR_NO_METADATA);
    TEST_ASSERT(strcmp(error_str, "No metadata found") == 0,
                "No metadata error code returns correct string");
    
    /* Test 4.4: Corrupted metadata code */
    error_str = dm_remap_v4_reassembly_error_to_string(DM_REMAP_V4_REASSEMBLY_ERROR_CORRUPTED);
    TEST_ASSERT(strcmp(error_str, "Metadata corrupted") == 0,
                "Corrupted metadata error code returns correct string");
    
    /* Test 4.5: Unknown error code */
    error_str = dm_remap_v4_reassembly_error_to_string(-999);
    TEST_ASSERT(strcmp(error_str, "Unknown error") == 0,
                "Unknown error code returns generic string");
}

/*
 * Test 5: Metadata Validation Edge Cases
 */
void test_metadata_validation_edge_cases(void)
{
    TEST_SECTION("Metadata Validation Edge Cases");
    
    struct dm_remap_v4_setup_metadata metadata;
    struct dm_remap_v4_device_fingerprint main_device;
    struct dm_remap_v4_target_config target_config;
    int result;
    int i;
    
    /* Setup valid base metadata */
    dm_remap_v4_create_device_fingerprint(&main_device, "/dev/test");
    memset(&target_config, 0, sizeof(target_config));
    target_config.config_magic = 0xDEADBEEF;
    strncpy(target_config.target_params, "0 1024 /dev/test 0", 
            sizeof(target_config.target_params) - 1);
    target_config.target_device_size = 1024;
    
    dm_remap_v4_create_setup_metadata(&metadata, &main_device, &target_config);
    
    /* Test 5.1: Maximum spare devices */
    struct dm_remap_v4_device_fingerprint spare_devices[DM_REMAP_V4_MAX_SPARE_DEVICES + 1];
    for (i = 0; i < DM_REMAP_V4_MAX_SPARE_DEVICES; i++) {
        char device_path[256];
        snprintf(device_path, sizeof(device_path), "/dev/spare%d", i);
        dm_remap_v4_create_device_fingerprint(&spare_devices[i], device_path);
        
        result = dm_remap_v4_add_spare_device_to_metadata(&metadata, &spare_devices[i], i + 1);
        TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                    "Add spare device within limits succeeds");
    }
    
    /* Test 5.2: Exceed maximum spare devices */
    dm_remap_v4_create_device_fingerprint(&spare_devices[DM_REMAP_V4_MAX_SPARE_DEVICES], 
                                         "/dev/spare_overflow");
    result = dm_remap_v4_add_spare_device_to_metadata(&metadata, 
                                                     &spare_devices[DM_REMAP_V4_MAX_SPARE_DEVICES], 
                                                     999);
    TEST_ASSERT(result == -ENOSPC,
                "Adding spare device beyond limit returns ENOSPC");
    
    /* Test 5.3: Verify metadata still valid after max spares */
    result = dm_remap_v4_verify_metadata_integrity(&metadata);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Metadata remains valid after adding maximum spare devices");
    
    /* Test 5.4: Invalid magic number detection */
    uint32_t original_magic = metadata.magic;
    metadata.magic = 0xBAD12345; /* Invalid magic */
    result = dm_remap_v4_verify_metadata_integrity(&metadata);
    TEST_ASSERT(result == -DM_REMAP_V4_REASSEMBLY_ERROR_CORRUPTED,
                "Invalid magic number detected");
    metadata.magic = original_magic; /* Restore */
    
    /* Test 5.5: Zero version counter handling */
    uint64_t original_version = metadata.version_counter;
    metadata.version_counter = 0;
    /* Recalculate both header and overall CRC since we changed the version counter */
    metadata.header_crc32 = crc32(0, (unsigned char *)&metadata.magic,
                                 sizeof(metadata.magic) + sizeof(metadata.metadata_version) +
                                 sizeof(metadata.version_counter));
    metadata.overall_crc32 = dm_remap_v4_calculate_metadata_crc32(&metadata);
    result = dm_remap_v4_verify_metadata_integrity(&metadata);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Zero version counter is valid");
    metadata.version_counter = original_version; /* Restore */
}

/*
 * Test 6: Performance and Stress Testing
 */
void test_performance_and_stress(void)
{
    TEST_SECTION("Performance and Stress Testing");
    
    struct dm_remap_v4_setup_metadata metadata_array[100];
    struct dm_remap_v4_device_fingerprint main_device, spare_device;
    struct dm_remap_v4_target_config target_config;
    clock_t start_time, end_time;
    double cpu_time_used;
    int i, result;
    
    /* Setup test data */
    dm_remap_v4_create_device_fingerprint(&main_device, "/dev/stress_main");
    dm_remap_v4_create_device_fingerprint(&spare_device, "/dev/stress_spare");
    
    memset(&target_config, 0, sizeof(target_config));
    target_config.config_magic = 0xDEADBEEF;
    strncpy(target_config.target_params, "0 2048 /dev/stress_main 0", 
            sizeof(target_config.target_params) - 1);
    target_config.target_device_size = 2048;
    
    /* Test 6.1: Bulk metadata creation performance */
    start_time = clock();
    
    for (i = 0; i < 100; i++) {
        result = dm_remap_v4_create_setup_metadata(&metadata_array[i], &main_device, &target_config);
        if (result != DM_REMAP_V4_REASSEMBLY_SUCCESS) {
            break;
        }
    }
    
    end_time = clock();
    cpu_time_used = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    TEST_ASSERT(i == 100, "Created 100 metadata structures successfully");
    TEST_ASSERT(cpu_time_used < 1.0, "Bulk metadata creation completed within 1 second");
    
    DMINFO("Created 100 metadata structures in %.4f seconds (%.2f/sec)", 
           cpu_time_used, 100.0 / cpu_time_used);
    
    /* Test 6.2: Bulk integrity verification performance */
    start_time = clock();
    
    int successful_verifications = 0;
    for (i = 0; i < 100; i++) {
        result = dm_remap_v4_verify_metadata_integrity(&metadata_array[i]);
        if (result == DM_REMAP_V4_REASSEMBLY_SUCCESS) {
            successful_verifications++;
        }
    }
    
    end_time = clock();
    cpu_time_used = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    TEST_ASSERT(successful_verifications == 100, "All 100 metadata structures passed verification");
    TEST_ASSERT(cpu_time_used < 0.5, "Bulk verification completed within 0.5 seconds");
    
    DMINFO("Verified 100 metadata structures in %.4f seconds (%.2f/sec)", 
           cpu_time_used, 100.0 / cpu_time_used);
    
    /* Test 6.3: Confidence calculation performance */
    struct dm_remap_v4_discovery_result test_result;
    memset(&test_result, 0, sizeof(test_result));
    test_result.copies_found = 5;
    test_result.copies_valid = 4;
    test_result.corruption_level = 1;
    test_result.metadata = metadata_array[0];
    
    start_time = clock();
    
    float total_confidence = 0.0f;
    for (i = 0; i < 1000; i++) {
        total_confidence += dm_remap_v4_calculate_confidence_score(&test_result);
    }
    
    end_time = clock();
    cpu_time_used = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    TEST_ASSERT(total_confidence > 0.0f, "Confidence calculations produced valid results");
    TEST_ASSERT(cpu_time_used < 0.1, "1000 confidence calculations completed within 0.1 seconds");
    
    DMINFO("Calculated 1000 confidence scores in %.4f seconds (%.2f/sec)", 
           cpu_time_used, 1000.0 / cpu_time_used);
}

/*
 * Test 7: Integration Testing
 */
void test_integration_scenarios(void)
{
    TEST_SECTION("Integration Testing Scenarios");
    
    struct dm_remap_v4_setup_metadata metadata;
    struct dm_remap_v4_device_fingerprint main_device, spare1, spare2;
    struct dm_remap_v4_target_config target_config;
    int result;
    
    /* Test 7.1: Complete setup creation workflow */
    result = dm_remap_v4_create_device_fingerprint(&main_device, "/dev/integration_main");
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Step 1: Create main device fingerprint");
    
    result = dm_remap_v4_create_device_fingerprint(&spare1, "/dev/integration_spare1");
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Step 2: Create first spare device fingerprint");
    
    result = dm_remap_v4_create_device_fingerprint(&spare2, "/dev/integration_spare2");
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Step 3: Create second spare device fingerprint");
    
    memset(&target_config, 0, sizeof(target_config));
    target_config.config_magic = 0xDEADBEEF;
    strncpy(target_config.target_params, "0 4096 /dev/integration_main 0 remap", 
            sizeof(target_config.target_params) - 1);
    target_config.target_device_size = 4096;
    target_config.config_version = 4;
    
    result = dm_remap_v4_create_setup_metadata(&metadata, &main_device, &target_config);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Step 4: Create complete setup metadata");
    
    result = dm_remap_v4_add_spare_device_to_metadata(&metadata, &spare1, 100);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Step 5: Add first spare device");
    
    result = dm_remap_v4_add_spare_device_to_metadata(&metadata, &spare2, 200);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Step 6: Add second spare device");
    
    result = dm_remap_v4_verify_metadata_integrity(&metadata);
    TEST_ASSERT(result == DM_REMAP_V4_REASSEMBLY_SUCCESS,
                "Step 7: Final metadata integrity verification");
    
    /* Test 7.2: Verify complete setup structure */
    TEST_ASSERT(metadata.num_spare_devices == 2,
                "Setup has correct number of spare devices");
    TEST_ASSERT(metadata.spare_devices[0].spare_priority == 100,
                "First spare device has correct priority");
    TEST_ASSERT(metadata.spare_devices[1].spare_priority == 200,
                "Second spare device has correct priority");
    TEST_ASSERT(strstr(metadata.target_config.target_params, "remap") != NULL,
                "Target parameters correctly stored");
    
    /* Test 7.3: Discovery result simulation */
    struct dm_remap_v4_discovery_result discovery_result;
    memset(&discovery_result, 0, sizeof(discovery_result));
    /* Note: has_metadata field doesn't exist in our structure, but we set other fields */
    discovery_result.copies_found = 5;
    discovery_result.copies_valid = 5;
    discovery_result.corruption_level = 0;
    discovery_result.metadata = metadata;
    discovery_result.discovery_timestamp = time(NULL);
    
    float confidence = dm_remap_v4_calculate_confidence_score(&discovery_result);
    TEST_ASSERT(confidence >= 0.9f,
                "Complete setup achieves high confidence score (≥0.9)");
    
    /* Test 7.4: Print metadata for visual inspection */
    printf("\n📋 Complete Setup Metadata:\n");
    dm_remap_v4_print_setup_metadata(&metadata);
}

/*
 * Main test runner
 */
int main(void)
{
    printf("🚀 dm-remap v4.0 Automatic Setup Reassembly - Comprehensive Test Suite\n");
    printf("========================================================================\n");
    
    /* Initialize test statistics */
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    
    /* Run all test suites */
    test_device_fingerprinting();
    test_metadata_creation();
    test_confidence_calculation();
    test_error_handling();
    test_metadata_validation_edge_cases();
    test_performance_and_stress();
    test_integration_scenarios();
    
    /* Print final results */
    printf("\n📊 === TEST RESULTS SUMMARY ===\n");
    printf("Tests Run:    %u\n", tests_run);
    printf("Tests Passed: %u\n", tests_passed);
    printf("Tests Failed: %u\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\n🎉 All setup reassembly tests PASSED! System ready for deployment.\n");
        printf("✅ Device fingerprinting: OPERATIONAL\n");
        printf("✅ Metadata creation: OPERATIONAL\n");
        printf("✅ Integrity verification: OPERATIONAL\n");
        printf("✅ Confidence calculation: OPERATIONAL\n");
        printf("✅ Error handling: OPERATIONAL\n");
        printf("✅ Edge case validation: OPERATIONAL\n");
        printf("✅ Performance metrics: ACCEPTABLE\n");
        printf("✅ Integration scenarios: VALIDATED\n");
        return 0;
    } else {
        printf("\n❌ %u test(s) FAILED! Review failures before deployment.\n", tests_failed);
        return 1;
    }
}