int dmr_hotpath_init(struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;
    
    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\n");
    
    if (!rc) {
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\n");
        return -EINVAL;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: About to allocate manager\n");
    
    /* THIS IS THE SUSPECT LINE - cache-aligned allocation */
    manager = dmr_alloc_cache_aligned(sizeof(*manager));
    
    printk(KERN_INFO "dmr_hotpath_init: Allocation completed\n");
    
    if (!manager) {
        printk(KERN_ERR "dmr_hotpath_init: Failed to allocate hotpath manager\n");
        return -ENOMEM;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: Setting manager and returning\n");
    rc->hotpath_manager = manager;
    
    printk(KERN_INFO "dmr_hotpath_init: TEST 1 completed successfully\n");
    return 0;
}
