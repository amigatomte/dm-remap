#!/bin/bash
echo "🔍 Scanning for direct kernel API calls that should use compat.h shims..."

# Look for any direct dm_per_bio_data calls outside compat.h
grep -RIn --exclude=compat.h "dm_per_bio_data" src/ | grep -v "dmr_per_bio_data" && \
    echo "⚠️ Found direct dm_per_bio_data calls — replace with dmr_per_bio_data()" || \
    echo "✅ No direct dm_per_bio_data calls found."

# Look for any direct bio_clone* calls outside compat.h
grep -RIn --exclude=compat.h "bio_clone" src/ | grep -v "dmr_bio_clone" && \
    echo "⚠️ Found direct bio_clone* calls — replace with dmr_bio_clone_*()" || \
    echo "✅ No direct bio_clone* calls found."

# Look for any direct bio_dup calls outside compat.h
grep -RIn --exclude=compat.h "bio_dup" src/ | grep -v "dmr_bio_clone" && \
    echo "⚠️ Found direct bio_dup calls — replace with dmr_bio_clone_*()" || \
    echo "✅ No direct bio_dup calls found."

echo "Scan complete."
