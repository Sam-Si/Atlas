import sys
import struct

def run_verification():
    print(f"[BAZEL VERIFIED] Running Python version: {sys.version.split()[0]}")
    print(f"[BAZEL VERIFIED] The 'struct' module is accessible: {dir(struct)[:3]}...")
    print(f"Ready for Prompt 1: The Heemut Generator.")

if __name__ == "__main__":
    run_verification()
