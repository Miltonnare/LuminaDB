#!/bin/bash -eu

# Fallback to standard -fsanitize=fuzzer if LIB_FUZZING_ENGINE is not set
if [ -z "${LIB_FUZZING_ENGINE+x}" ]; then
  export LIB_FUZZING_ENGINE="-fsanitize=fuzzer"
fi

echo "Building with LIB_FUZZING_ENGINE: $LIB_FUZZING_ENGINE"

# Create a build directory
mkdir -p build
cd build

# Run CMake
cmake .. \
  -DBUILD_TESTS=OFF \
  -DBUILD_FUZZERS=ON \
  -DENABLE_SANITIZERS=ON \
  -DCMAKE_BUILD_TYPE=Release

# Compile fuzzer targets
make -j$(nproc)

# Copy executable binaries to $OUT
cp query_fuzzer "$OUT/"
cp storage_fuzzer "$OUT/"
cp tlv_fuzzer "$OUT/"

# Prepare seed corpora
cd ..
mkdir -p seed_corpus/query
echo "CREATE TABLE t1 (id INT PRIMARY KEY, name VARCHAR);" > seed_corpus/query/create.sql
echo "SELECT id, name FROM t1 WHERE id = 12" > seed_corpus/query/select.sql
echo "INSERT INTO t1 VALUES (1, 'Alice')" > seed_corpus/query/insert.sql
zip -j "$OUT/query_fuzzer_seed_corpus.zip" seed_corpus/query/*

mkdir -p seed_corpus/storage
echo -ne "\x00\x01\x00\x04test" > seed_corpus/storage/insert_seed
echo -ne "\x01\x01\x00\x04test" > seed_corpus/storage/update_seed
echo -ne "\x02\x01\x00\x00" > seed_corpus/storage/delete_seed
zip -j "$OUT/storage_fuzzer_seed_corpus.zip" seed_corpus/storage/*

mkdir -p seed_corpus/tlv
# Serialized INT value 42
echo -ne "\x01\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\x00\x2a" > seed_corpus/tlv/int_42
# Serialized VARCHAR value "Hello"
echo -ne "\x03\x00\x00\x00\x05Hello" > seed_corpus/tlv/varchar_hello
zip -j "$OUT/tlv_fuzzer_seed_corpus.zip" seed_corpus/tlv/*

echo "Fuzz build complete."
