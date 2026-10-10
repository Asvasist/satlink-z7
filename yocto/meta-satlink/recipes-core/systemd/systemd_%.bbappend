# systemd 255.22 (poky scarthgap) already contains this fix; meta-xilinx still
# backports it, so the patch would be applied twice.
SRC_URI:remove = "file://0001-socket-downgrade-not-supported-logging-for-SO_PASSSE.patch"
