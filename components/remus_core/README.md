# remus_core

Pure C++17 types and bounded containers shared by both images. The caller owns
queue/pool lifetime and synchronization. Full containers reject new writes;
they never overwrite live entries. Host coverage lives in `tests/host/unit`.
