idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "certs/servercert.pem"
                                   "certs/prvtkey.pem")


# Create a FATFS image from the contents of the 'fatfs_image' directory
# that fits the partition named 'storage'. FLASH_IN_PROJECT indicates that
# the generated image should be flashed when the entire project is flashed to
# the target with 'idf.py -p PORT flash'.
# If read-only mode is set (CONFIG_EXAMPLE_FATFS_MODE_READ_ONLY)
# the generated image will be raw without wear levelling support.
# Otherwise it will support wear levelling and thus enable read-write mounting of the image in the device.

set(image ../data)
fatfs_create_spiflash_image(storage ${image} FLASH_IN_PROJECT PRESERVE_TIME)