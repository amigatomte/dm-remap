# dm-remap v3.0 - PERSISTENCE & RECOVERY Implementation Plan

## 🎯 **Objective**
Add persistent remap table storage with crash recovery capabilities, making dm-remap suitable for critical production systems.

## 🏗️ **Architecture Overview**

### **Metadata Storage Design**
```
Spare Device Layout:
┌─────────────────┬─────────────────┬─────────────────┐
│ Metadata Header │ Remap Table     │ Data Sectors    │
│ (4KB)           │ (Variable)      │ (Remainder)     │
└─────────────────┴─────────────────┴─────────────────┘

Metadata Header (4KB):
[Magic: "DMREMAP3"][Version: 1][Checksum: CRC32][Timestamp]
[Remap Count: N][Reserved: padding to 4KB]

Remap Table Entry (16 bytes each):
[Main Sector: 8B][Spare Sector: 8B]
```

### **Key Features**
1. **Persistent Storage**: Remap table stored in spare device header
2. **Crash Recovery**: Automatic restore on device activation
3. **Atomic Updates**: Safe metadata updates with checksums
4. **Hot-plug Support**: Enable spare device replacement
5. **Migration Tools**: Move remap data between devices

## 📋 **Implementation Phases**

### **Phase 1: Metadata Infrastructure** (Week 1)
- [ ] Define metadata format and structures
- [ ] Implement metadata I/O functions
- [ ] Add checksum validation
- [ ] Create metadata initialization

### **Phase 2: Persistence Engine** (Week 2)
- [ ] Implement remap table serialization
- [ ] Add metadata write/read functions
- [ ] Implement atomic update mechanism
- [ ] Add error recovery for corrupted metadata

### **Phase 3: Recovery System** (Week 3)
- [ ] Implement device activation recovery
- [ ] Add remap table restoration
- [ ] Implement consistency checking
- [ ] Add recovery failure handling

### **Phase 4: Management Interface** (Week 4)
- [ ] Add new message commands (save/restore/migrate)
- [ ] Implement spare device replacement
- [ ] Add metadata status reporting
- [ ] Create migration utilities

## 🔧 **New Message Commands**

```bash
# Force metadata sync to spare device
echo "save" > /sys/kernel/debug/dm-remap/metadata_control

# Reload remap table from metadata  
echo "restore" > /sys/kernel/debug/dm-remap/metadata_control

# Migrate to new spare device
echo "migrate /dev/newspare" > /sys/kernel/debug/dm-remap/metadata_control

# Show metadata status
cat /sys/kernel/debug/dm-remap/metadata_status
```

## 📊 **Enhanced Status Format**

```bash
# v3.0 status will include persistence info
0 204800 remap v3.0 2/1000 0/1000 2/1000 health=1 errors=W0:R0 auto_remaps=0 manual_remaps=2 scan=0% persist=enabled last_save=1696195200
```

## 🧪 **Testing Strategy**

### **Persistence Tests**
- [ ] Metadata write/read validation
- [ ] Checksum integrity testing
- [ ] Atomic update verification
- [ ] Corruption recovery testing

### **Recovery Tests**
- [ ] Clean shutdown/startup
- [ ] Crash simulation and recovery
- [ ] Partial metadata corruption
- [ ] Spare device replacement

### **Migration Tests**
- [ ] Device replacement scenarios
- [ ] Data migration validation
- [ ] Rollback capability
- [ ] Error handling during migration

## 🎯 **Success Criteria**

1. **Data Persistence**: Remap tables survive system reboots
2. **Crash Recovery**: Automatic restoration after unexpected shutdowns
3. **Data Integrity**: Zero data loss during persistence operations
4. **Performance**: Minimal impact on I/O performance
5. **Reliability**: Robust error handling and recovery mechanisms

## 📅 **Timeline**

- **Week 1**: Metadata infrastructure
- **Week 2**: Persistence engine
- **Week 3**: Recovery system  
- **Week 4**: Management & testing
- **Total**: ~1 month development cycle

## 🚀 **Getting Started**

First implementation step: Create metadata structures and basic I/O functions.

---

*This plan builds on the solid v2.1 foundation to add enterprise-grade persistence capabilities.*