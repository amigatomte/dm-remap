# dm-remap v3.0 Phase 3: Recovery System Implementation Plan

## 🎯 Phase 3 Objectives

**Goal**: Integrate persistence engine with main dm-remap target and implement complete recovery system

**Duration**: 1-2 days  
**Prerequisites**: ✅ Phase 1 (Metadata Infrastructure) + ✅ Phase 2 (Persistence Engine)

---

## 🔧 Implementation Tasks

### Task 1: Main Target Integration
**File**: `dm_remap_main.c`
- Integrate metadata context creation during target construction
- Add metadata initialization to `dm_remap_ctr()`  
- Connect remap operations with metadata persistence
- Ensure metadata cleanup in `dm_remap_dtr()`

### Task 2: Boot-time Recovery
**File**: `dm-remap-recovery.c` (new)
- Implement device activation recovery
- Read existing metadata during target creation
- Restore remap table from persistent storage
- Handle metadata corruption gracefully

### Task 3: Remap Integration
**Files**: `dm_remap_io.c`, `dm_remap_production.c`
- Connect sector remapping with metadata updates
- Auto-save trigger on new remap creation
- Ensure metadata consistency with remap operations
- Performance optimization for metadata updates

### Task 4: Message Interface
**File**: `dm_remap_messages.c`
- Add `save` command - force metadata synchronization
- Add `restore` command - reload from persistent storage  
- Add `metadata_status` command - show persistence state
- Add `migrate` command - move to new spare device

### Task 5: Enhanced Testing
**File**: `test_recovery_v3.sh` (new)
- End-to-end recovery testing
- Simulate system reboot scenarios
- Test metadata corruption recovery
- Validate remap persistence across device recreation

---

## 📋 Implementation Checklist

### ✅ Ready to Start
- [x] Phase 2 persistence engine complete
- [x] I/O operations functional  
- [x] Auto-save system working
- [x] Module parameters available

### 🔄 Phase 3 Tasks
- [ ] **Task 1**: Main target integration
- [ ] **Task 2**: Boot-time recovery implementation  
- [ ] **Task 3**: Remap operation integration
- [ ] **Task 4**: Management command interface
- [ ] **Task 5**: Comprehensive testing framework

---

## 🎯 Success Criteria

1. **Device Creation Recovery**: dm-remap target reads existing metadata on creation
2. **Automatic Persistence**: New remaps automatically saved to spare device
3. **Boot Survival**: Remap table survives system reboot
4. **Management Interface**: Commands available via dmsetup message
5. **Error Recovery**: Graceful handling of metadata corruption
6. **Performance**: Minimal impact on I/O operations

---

## 🧪 Testing Strategy

### Recovery Scenarios
1. **Normal Recovery**: Device recreation reads existing remaps
2. **Corruption Recovery**: Handles corrupted metadata gracefully  
3. **Empty Metadata**: Clean initialization on first use
4. **Migration**: Move metadata to new spare device

### Performance Testing
1. **I/O Impact**: Measure overhead of metadata operations
2. **Auto-save Performance**: Background save efficiency
3. **Recovery Time**: Device activation speed with existing metadata

---

*Phase 3 will complete the v3.0 implementation, providing full enterprise-grade persistence and recovery capabilities.*