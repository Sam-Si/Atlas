#!/usr/bin/env python3
import sys
import struct
import os
import random
import time

def generate_binary_data(output_path, total_size_gb=10, chunk_size_records=100_000):
    """
    Prompt 1: The Heemut Generator
    Generates a massive binary file with 128-byte records.
    - timestamp: 64-bit uint (Sort key)
    - feature_id: 32-bit uint
    - payload: 116-byte random char array
    """
    record_size = 128
    total_records = (total_size_gb * 1024 * 1024 * 1024) // record_size
    
    print(f"Generating {total_size_gb}GB binary file ({total_records:,} records)...")
    print(f"Output: {output_path}")

    # Binary format: Q (64-bit uint), I (32-bit uint), 116s (116-byte string)
    record_format = struct.Struct("<Q I 116s")
    
    start_time = time.time()
    
    try:
        with open(output_path, "wb") as f:
            records_written = 0
            while records_written < total_records:
                batch_size = min(chunk_size_records, total_records - records_written)
                
                # Pre-generate a chunk of records in memory to avoid too many syscalls
                chunk_data = bytearray()
                for _ in range(batch_size):
                    # Random timestamp and feature_id
                    timestamp = random.randint(0, 2**64 - 1)
                    feature_id = random.randint(0, 2**32 - 1)
                    payload = os.urandom(116)
                    
                    chunk_data.extend(record_format.pack(timestamp, feature_id, payload))
                
                f.write(chunk_data)
                records_written += batch_size
                
                # Progress every 10%
                percent = (records_written / total_records) * 100
                if records_written % (chunk_size_records * 10) == 0 or records_written == total_records:
                    elapsed = time.time() - start_time
                    rate = (records_written * record_size) / (1024 * 1024 * elapsed)
                    print(f"  Progress: {percent:.1f}% ({records_written:,} records) | {rate:.2f} MB/s")
                    
        print(f"\nSuccess! Generated {output_path} in {time.time() - start_time:.2f} seconds.")
        
    except KeyboardInterrupt:
        print("\nGeneration interrupted. Cleaning up...")
        if os.path.exists(output_path):
            os.remove(output_path)
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: generator.py <output_path> [size_gb]")
        sys.exit(1)
    
    path = sys.argv[1]
    size = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1 # Default to 100MB for testing
    generate_binary_data(path, total_size_gb=size)
