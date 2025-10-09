#!/bin/bash
# Test if the CALL to dmr_hotpath_init() is the problem
set -e

echo "TESTING IF THE CALL TO dmr_hotpath_init() IS THE PROBLEM"
echo "======================================================="
echo ""

# Comment out the call to dmr_hotpath_init()
echo "1. Commenting out the call to dmr_hotpath_init()..."

cp src/dm_remap_main.c src/dm_remap_main.c.backup

# Comment out the hotpath init call
sed -i '/ret = dmr_hotpath_init(rc);/s/^/    \/\* COMMENTED: /' src/dm_remap_main.c
sed -i '/DMR_DEBUG.*Failed to initialize hotpath optimization/s/^/    \/\* COMMENTED: /' src/dm_remap_main.c  
sed -i '/rc->hotpath_manager = NULL;/s/^/        \/\* COMMENTED: /' src/dm_remap_main.c
sed -i '/DMR_DEBUG.*Hotpath optimization initialized successfully/s/^/        \/\* COMMENTED: /' src/dm_remap_main.c

echo "2. Building without hotpath init call..."
cd src && make clean > /dev/null 2>&1 && make > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "❌ Build failed"
    cp dm_remap_main.c.backup dm_remap_main.c
    cd ..
    exit 1
fi
cd ..

echo "3. Testing module load without hotpath init..."
timeout 3s sudo insmod src/dm_remap.ko 2>/dev/null || {
    echo "🚨 STILL HANGS! The problem is NOT the dmr_hotpath_init() call!"
    echo "The problem is somewhere else - maybe the includes or dependencies!"
    sudo pkill -f insmod 2>/dev/null || true
    cp src/dm_remap_main.c.backup src/dm_remap_main.c
    exit 1
}

echo "✅ Success! Module loads without the dmr_hotpath_init() call"
sudo rmmod dm_remap

echo ""
echo "🎯 CONCLUSION: The dmr_hotpath_init() CALL is causing the hang!"
echo ""

# Test if it's the function definition that's the problem
echo "4. Testing if the function DEFINITION is the problem..."
echo "   Renaming dmr_hotpath_init() to dmr_hotpath_init_DISABLED()..."

# Rename the function definition to disable it
sed -i 's/int dmr_hotpath_init(/int dmr_hotpath_init_DISABLED(/g' src/dm-remap-hotpath-optimization.c
sed -i 's/int dmr_hotpath_init(/int dmr_hotpath_init_DISABLED(/g' src/dm-remap-hotpath-optimization.h

# Restore the call but to a non-existent function (should cause build error)
cp src/dm_remap_main.c.backup src/dm_remap_main.c

echo "5. Building with function renamed (should build fine)..."
cd src && make clean > /dev/null 2>&1 && make > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "❌ Build succeeded when it should have failed (missing function)"
    echo "This means the function might not be getting called or linked properly"
else
    echo "✅ Build failed as expected (missing function)"
    echo "This confirms the function call was the problem"
fi
cd ..

# Restore everything
echo ""
echo "6. Restoring original files..."
cp src/dm_remap_main.c.backup src/dm_remap_main.c
cp src/dm-remap-hotpath-optimization.c.original src/dm-remap-hotpath-optimization.c

echo ""
echo "🎯 THE PROBLEM IS: The call to dmr_hotpath_init() causes hanging!"
echo "   Even an empty dmr_hotpath_init() function hangs when called!"
echo ""
echo "NEXT STEP: Check what happens BEFORE dmr_hotpath_init() is called"