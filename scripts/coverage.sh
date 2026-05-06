#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."

BUILD=build_coverage
mkdir -p "$BUILD"
cd "$BUILD"

cmake .. -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
make -j"$(nproc)" run_tests

./run_tests

lcov --capture \
     --directory . \
     --output-file coverage.info \
     --no-external \
     --base-directory .. \
     --ignore-errors mismatch,inconsistent

lcov --remove coverage.info \
     "*/_deps/*" "*/tests/*" \
     --output-file coverage_filtered.info \
     --ignore-errors mismatch,inconsistent

genhtml coverage_filtered.info \
        --output-directory coverage_html \
        --legend \
        --title "hms-gmail"

echo ""
lcov --list coverage_filtered.info --ignore-errors mismatch,inconsistent
echo ""
echo "Report: $PWD/coverage_html/index.html"
