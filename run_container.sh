#!/bin/bash
# run_container.sh - A helper script to quickly rebuild and execute the atlas sandbox

set -e

IMAGE_NAME="out_of_core_sorter_env"

echo "==== BUILDING DOCKER ENVIRONMENT ===="
docker build -t $IMAGE_NAME .

echo "==== STARTING DOCKER CONTAINER ===="
echo "Note: Memory strictly limited to 500m (Swap 500m)."
echo "Mounting $(pwd) inside the container at /testbed/atlas"

# --cap-add=SYS_PTRACE is required for strace and valgrind to hook into memory properly
docker run --rm -it \
  --memory="500m" \
  --memory-swap="500m" \
  --cap-add=SYS_PTRACE \
  -v "$(pwd)":/testbed/atlas \
  $IMAGE_NAME \
  /bin/bash
