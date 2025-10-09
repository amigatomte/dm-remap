/*
 * Test version of dmr_hotpath_init() - Basic validation
 */

int dmr_hotpath_init(struct remap_c *rc)
{
    if (!rc) return -EINVAL;
    
    return 0;
}
