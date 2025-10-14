// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_v4_spare_pool.c - Test suite for spare pool management
 *
 * Copyright (C) 2025 dm-remap development team
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include "../include/dm-remap-v4-spare-pool.h"

#define TEST_PASS(name) pr_info("[PASS] %s\n", name)
#define TEST_FAIL(name, ...) pr_err("[FAIL] %s: " __VA_ARGS__, name)

static int tests_passed = 0;
static int tests_failed = 0;

/*
 * Test 1: Initialize and cleanup spare pool
 */
static int test_spare_pool_init_exit(void)
{
	struct spare_pool pool;
	int ret;
	
	ret = spare_pool_init(&pool, NULL);
	if (ret != 0) {
		TEST_FAIL("spare_pool_init", "returned %d\n", ret);
		return -1;
	}
	
	if (atomic_read(&pool.spare_device_count) != 0) {
		TEST_FAIL("spare_pool_init", "device count = %d, expected 0\n",
			  atomic_read(&pool.spare_device_count));
		spare_pool_exit(&pool);
		return -1;
	}
	
	spare_pool_exit(&pool);
	TEST_PASS("spare_pool_init_exit");
	return 0;
}

/*
 * Test 2: Allocation and free (simulated, no real devices)
 */
static int test_spare_allocation_lifecycle(void)
{
	struct spare_pool pool;
	struct spare_allocation *alloc;
	struct spare_device *spare;
	int ret;
	
	/* Initialize pool */
	ret = spare_pool_init(&pool, NULL);
	if (ret != 0) {
		TEST_FAIL("allocation_lifecycle", "pool init failed\n");
		return -1;
	}
	
	/* Simulate adding a spare device */
	spare = kzalloc(sizeof(*spare), GFP_KERNEL);
	if (!spare) {
		TEST_FAIL("allocation_lifecycle", "spare alloc failed\n");
		spare_pool_exit(&pool);
		return -1;
	}
	
	spare->total_sectors = 1024 * 1024; /* 512 MB */
	spare->free_sectors = spare->total_sectors;
	spare->allocation_unit = pool.allocation_unit;
	spare->state = SPARE_STATE_AVAILABLE;
	spare->bitmap_size = BITS_TO_LONGS(spare->total_sectors / spare->allocation_unit);
	spare->allocation_bitmap = kcalloc(spare->bitmap_size, sizeof(unsigned long), GFP_KERNEL);
	spare->dev_path = kstrdup("/dev/test", GFP_KERNEL);
	
	if (!spare->allocation_bitmap || !spare->dev_path) {
		TEST_FAIL("allocation_lifecycle", "bitmap/path alloc failed\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	spin_lock_init(&spare->lock);
	atomic_set(&spare->refcount, 0);
	
	/* Add to pool manually (bypass device open) */
	list_add_tail(&spare->list, &pool.spares);
	atomic_inc(&pool.spare_device_count);
	
	/* Try allocation */
	alloc = spare_pool_allocate(&pool, 1000, 8);
	if (IS_ERR(alloc)) {
		TEST_FAIL("allocation_lifecycle", "allocation failed: %ld\n",
			  PTR_ERR(alloc));
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Verify allocation */
	if (alloc->original_sector != 1000) {
		TEST_FAIL("allocation_lifecycle", "wrong original sector\n");
		spare_pool_free(&pool, alloc);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	if (atomic_read(&pool.allocation_count) != 1) {
		TEST_FAIL("allocation_lifecycle", "allocation count wrong\n");
		spare_pool_free(&pool, alloc);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Free allocation */
	ret = spare_pool_free(&pool, alloc);
	if (ret != 0) {
		TEST_FAIL("allocation_lifecycle", "free failed: %d\n", ret);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	if (atomic_read(&pool.allocation_count) != 0) {
		TEST_FAIL("allocation_lifecycle", "allocation not freed\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Cleanup */
	kfree(spare->allocation_bitmap);
	kfree(spare->dev_path);
	kfree(spare);
	spare_pool_exit(&pool);
	
	TEST_PASS("allocation_lifecycle");
	return 0;
}

/*
 * Test 3: Multiple allocations
 */
static int test_multiple_allocations(void)
{
	struct spare_pool pool;
	struct spare_allocation *alloc1, *alloc2, *alloc3;
	struct spare_device *spare;
	int ret;
	
	/* Initialize pool */
	ret = spare_pool_init(&pool, NULL);
	if (ret != 0) {
		TEST_FAIL("multiple_allocations", "pool init failed\n");
		return -1;
	}
	
	/* Simulate adding a spare device */
	spare = kzalloc(sizeof(*spare), GFP_KERNEL);
	if (!spare) {
		TEST_FAIL("multiple_allocations", "spare alloc failed\n");
		spare_pool_exit(&pool);
		return -1;
	}
	
	spare->total_sectors = 1024 * 1024;
	spare->free_sectors = spare->total_sectors;
	spare->allocation_unit = pool.allocation_unit;
	spare->state = SPARE_STATE_AVAILABLE;
	spare->bitmap_size = BITS_TO_LONGS(spare->total_sectors / spare->allocation_unit);
	spare->allocation_bitmap = kcalloc(spare->bitmap_size, sizeof(unsigned long), GFP_KERNEL);
	spare->dev_path = kstrdup("/dev/test", GFP_KERNEL);
	
	if (!spare->allocation_bitmap || !spare->dev_path) {
		TEST_FAIL("multiple_allocations", "bitmap/path alloc failed\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	spin_lock_init(&spare->lock);
	atomic_set(&spare->refcount, 0);
	list_add_tail(&spare->list, &pool.spares);
	atomic_inc(&pool.spare_device_count);
	
	/* Allocate three times */
	alloc1 = spare_pool_allocate(&pool, 100, 8);
	alloc2 = spare_pool_allocate(&pool, 200, 16);
	alloc3 = spare_pool_allocate(&pool, 300, 8);
	
	if (IS_ERR(alloc1) || IS_ERR(alloc2) || IS_ERR(alloc3)) {
		TEST_FAIL("multiple_allocations", "one or more allocations failed\n");
		if (!IS_ERR(alloc1)) spare_pool_free(&pool, alloc1);
		if (!IS_ERR(alloc2)) spare_pool_free(&pool, alloc2);
		if (!IS_ERR(alloc3)) spare_pool_free(&pool, alloc3);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Verify count */
	if (atomic_read(&pool.allocation_count) != 3) {
		TEST_FAIL("multiple_allocations", "allocation count = %d, expected 3\n",
			  atomic_read(&pool.allocation_count));
		spare_pool_free(&pool, alloc1);
		spare_pool_free(&pool, alloc2);
		spare_pool_free(&pool, alloc3);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Free all */
	spare_pool_free(&pool, alloc1);
	spare_pool_free(&pool, alloc2);
	spare_pool_free(&pool, alloc3);
	
	if (atomic_read(&pool.allocation_count) != 0) {
		TEST_FAIL("multiple_allocations", "not all allocations freed\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Cleanup */
	kfree(spare->allocation_bitmap);
	kfree(spare->dev_path);
	kfree(spare);
	spare_pool_exit(&pool);
	
	TEST_PASS("multiple_allocations");
	return 0;
}

/*
 * Test 4: Lookup allocation
 */
static int test_allocation_lookup(void)
{
	struct spare_pool pool;
	struct spare_allocation *alloc, *found;
	struct spare_device *spare;
	int ret;
	
	/* Initialize pool */
	ret = spare_pool_init(&pool, NULL);
	if (ret != 0) {
		TEST_FAIL("allocation_lookup", "pool init failed\n");
		return -1;
	}
	
	/* Simulate spare device */
	spare = kzalloc(sizeof(*spare), GFP_KERNEL);
	if (!spare) {
		TEST_FAIL("allocation_lookup", "spare alloc failed\n");
		spare_pool_exit(&pool);
		return -1;
	}
	
	spare->total_sectors = 1024 * 1024;
	spare->free_sectors = spare->total_sectors;
	spare->allocation_unit = pool.allocation_unit;
	spare->state = SPARE_STATE_AVAILABLE;
	spare->bitmap_size = BITS_TO_LONGS(spare->total_sectors / spare->allocation_unit);
	spare->allocation_bitmap = kcalloc(spare->bitmap_size, sizeof(unsigned long), GFP_KERNEL);
	spare->dev_path = kstrdup("/dev/test", GFP_KERNEL);
	
	if (!spare->allocation_bitmap || !spare->dev_path) {
		TEST_FAIL("allocation_lookup", "bitmap/path alloc failed\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	spin_lock_init(&spare->lock);
	atomic_set(&spare->refcount, 0);
	list_add_tail(&spare->list, &pool.spares);
	atomic_inc(&pool.spare_device_count);
	
	/* Allocate */
	alloc = spare_pool_allocate(&pool, 5000, 8);
	if (IS_ERR(alloc)) {
		TEST_FAIL("allocation_lookup", "allocation failed\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Lookup by original sector */
	found = spare_pool_lookup_allocation(&pool, 5000);
	if (found != alloc) {
		TEST_FAIL("allocation_lookup", "lookup failed (found=%p, expected=%p)\n",
			  found, alloc);
		spare_pool_free(&pool, alloc);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Lookup non-existent */
	found = spare_pool_lookup_allocation(&pool, 9999);
	if (found != NULL) {
		TEST_FAIL("allocation_lookup", "found non-existent allocation\n");
		spare_pool_free(&pool, alloc);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Cleanup */
	spare_pool_free(&pool, alloc);
	kfree(spare->allocation_bitmap);
	kfree(spare->dev_path);
	kfree(spare);
	spare_pool_exit(&pool);
	
	TEST_PASS("allocation_lookup");
	return 0;
}

/*
 * Test 5: Statistics
 */
static int test_spare_pool_stats(void)
{
	struct spare_pool pool;
	struct spare_pool_stats stats;
	struct spare_allocation *alloc;
	struct spare_device *spare;
	int ret;
	
	/* Initialize pool */
	ret = spare_pool_init(&pool, NULL);
	if (ret != 0) {
		TEST_FAIL("spare_pool_stats", "pool init failed\n");
		return -1;
	}
	
	/* Simulate spare device */
	spare = kzalloc(sizeof(*spare), GFP_KERNEL);
	if (!spare) {
		TEST_FAIL("spare_pool_stats", "spare alloc failed\n");
		spare_pool_exit(&pool);
		return -1;
	}
	
	spare->total_sectors = 2048; /* Small for testing */
	spare->free_sectors = spare->total_sectors;
	spare->allocation_unit = pool.allocation_unit;
	spare->state = SPARE_STATE_AVAILABLE;
	spare->bitmap_size = BITS_TO_LONGS(spare->total_sectors / spare->allocation_unit);
	spare->allocation_bitmap = kcalloc(spare->bitmap_size, sizeof(unsigned long), GFP_KERNEL);
	spare->dev_path = kstrdup("/dev/test", GFP_KERNEL);
	
	if (!spare->allocation_bitmap || !spare->dev_path) {
		TEST_FAIL("spare_pool_stats", "bitmap/path alloc failed\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	spin_lock_init(&spare->lock);
	atomic_set(&spare->refcount, 0);
	list_add_tail(&spare->list, &pool.spares);
	atomic_inc(&pool.spare_device_count);
	atomic64_add(spare->total_sectors, &pool.total_spare_capacity);
	
	/* Get initial stats */
	spare_pool_get_stats(&pool, &stats);
	
	if (stats.spare_device_count != 1) {
		TEST_FAIL("spare_pool_stats", "initial device count wrong\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	if (stats.total_capacity != 2048) {
		TEST_FAIL("spare_pool_stats", "initial capacity wrong\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Allocate and check stats */
	alloc = spare_pool_allocate(&pool, 100, 8);
	if (IS_ERR(alloc)) {
		TEST_FAIL("spare_pool_stats", "allocation failed\n");
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	spare_pool_get_stats(&pool, &stats);
	
	if (stats.active_allocations != 1) {
		TEST_FAIL("spare_pool_stats", "active allocations wrong\n");
		spare_pool_free(&pool, alloc);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	if (stats.allocated_capacity != 8) {
		TEST_FAIL("spare_pool_stats", "allocated capacity wrong\n");
		spare_pool_free(&pool, alloc);
		kfree(spare->allocation_bitmap);
		kfree(spare->dev_path);
		kfree(spare);
		spare_pool_exit(&pool);
		return -1;
	}
	
	/* Cleanup */
	spare_pool_free(&pool, alloc);
	kfree(spare->allocation_bitmap);
	kfree(spare->dev_path);
	kfree(spare);
	spare_pool_exit(&pool);
	
	TEST_PASS("spare_pool_stats");
	return 0;
}

/*
 * Run all tests
 */
static int __init test_spare_pool_init(void)
{
	pr_info("=== dm-remap v4 Spare Pool Test Suite ===\n");
	
	if (test_spare_pool_init_exit() == 0)
		tests_passed++;
	else
		tests_failed++;
	
	if (test_spare_allocation_lifecycle() == 0)
		tests_passed++;
	else
		tests_failed++;
	
	if (test_multiple_allocations() == 0)
		tests_passed++;
	else
		tests_failed++;
	
	if (test_allocation_lookup() == 0)
		tests_passed++;
	else
		tests_failed++;
	
	if (test_spare_pool_stats() == 0)
		tests_passed++;
	else
		tests_failed++;
	
	pr_info("=== Test Results: %d passed, %d failed ===\n",
		tests_passed, tests_failed);
	
	return tests_failed > 0 ? -EINVAL : 0;
}

static void __exit test_spare_pool_exit(void)
{
	pr_info("Spare pool test module unloaded\n");
}

module_init(test_spare_pool_init);
module_exit(test_spare_pool_exit);

MODULE_AUTHOR("dm-remap development team");
MODULE_DESCRIPTION("Test suite for dm-remap v4 spare pool management");
MODULE_LICENSE("GPL");
