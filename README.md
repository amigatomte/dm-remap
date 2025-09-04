# dm-remap
dm-remap is a custom Linux Device Mapper (DM) target that emulates firmware-level bad sector remapping in software. It allows you to dynamically redirect I/O from failing sectors to a reserved pool of spare sectors, creating a resilient virtual block device on top of degraded hardware.
