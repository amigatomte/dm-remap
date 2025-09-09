#!/bin/bash
echo "🔍 Listing all uses of compat.h shims in your source tree..."

echo
echo "📌 dmr_per_bio_data() usage:"
grep -RIn --exclude=compat.h "dmr_per_bio_data" src/ || echo "❌ None found"

echo
echo "📌 dmr_bio_clone_shallow() usage:"
grep -RIn --exclude=compat.h "dmr_bio_clone_shallow" src/ || echo "❌ None found"

echo
echo "📌 dmr_bio_clone_deep() usage:"
grep -RIn --exclude=compat.h "dmr_bio_clone_deep" src/ || echo "❌ None found"

echo
echo "✅ Scan complete."
