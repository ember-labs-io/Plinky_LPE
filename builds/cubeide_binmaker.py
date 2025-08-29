# this script takes the compiled firmware (sw/Release/plinkyblack.bin) and
# bundles it with a bootloader (golden_bootloader.bin) into a single binary file
# with a version number and filename extracted from the firmware.
# it USED to read the bootloader from (bootloader/Release/plinkybl.bin) but
# to avoid unnecessary flashing, it now reads the bootloader from the copy.
# if you want to update the bootloader, you need to copy it over the golden_bootloader.bin
# and rerun this script with the magic argument 'bootloader' 

import re

# Configuration
FIRMWARE_PREFIX = "PlinkyLPE"

def find_version_in_binary(binary_data):
    """
    Find version strings in binary data using regex.
    Returns the last version found in format X.X.X
    """
    # Convert binary data to string, ignoring non-ASCII bytes
    try:
        text = binary_data.decode('ascii', errors='ignore')
    except:
        text = ''.join(chr(b) for b in binary_data if 32 <= b <= 126)
    
    # Regex pattern for version strings like "1.2.3"
    # Matches: digit(s), dot, digit(s), dot, digit(s)
    version_pattern = r'(\d+\.\d+\.\d+)'
    
    # Find all version matches
    matches = re.findall(version_pattern, text)
    
    if matches:
        # Return the last version found (most likely to be the current version)
        return matches[-1]
    
    return None

def main():
    # bin file maker
    bootloader_file = 'golden_bootloader.bin' # "bootloader/Release/plinkybl.bin"
    try:
        with open(bootloader_file, "rb") as f1:
            bl_content = f1.read()
        with open("../sw/Release/plinkyblack.bin", "rb") as f2:
            app_content = f2.read()
    except IOError as e:
        print(f"Failed to open file: {e}")
        exit(2)
    appsize = len(app_content)
    bootloader_version = "0000"
    if chr(bl_content[-3])=='.':
        bootloader_version = chr(bl_content[-4]) + chr(bl_content[-3]) + chr(bl_content[-2]) + chr(bl_content[-1])
    print(f'SIZES: {appsize} app, {len(bl_content)} bootloader, bootloader version [{bootloader_version}]')
    # pad bl_content and app_content to reach their sizes
    bl_content += b'\xff' * (65536 - len(bl_content) - 4)
    bl_content += bootloader_version.encode('ascii')
    assert(len(bl_content) == 65536)
    app_content += b'\xff' * (1024 * 1024 - 65536 - 2048 - len(app_content)) # leave the last sector empty for the calibration

    app = bl_content + app_content

    # Extract version using regex-based approach
    version_str = find_version_in_binary(app_content)
    if version_str:
        # Parse version components
        version_parts = version_str.split('.')  # "X.Y.Z" -> ["X", "Y", "Z"]
        ver_major = version_parts[0]
        ver_minor = version_parts[1] 
        ver_patch = version_parts[2]
        print(f"Found version: {version_str}")
    else:
        print("!!!!!!!!!!!!!!!! NO VERSION FOUND IN BIN FILE")
        print("Searched for patterns like '1.2.3' in the binary data")
        exit(2)
    print(f"bootloader size {len(bl_content)}, app size {appsize}, version {ver_major}.{ver_minor}.{ver_patch}")
    fname = f"{FIRMWARE_PREFIX}-{ver_major}.{ver_minor}.{ver_patch}.bin"
    print(f'outputting {fname}...')
    try:
        with open(fname, "wb") as fo:
            fo.write(app)
    except IOError as e:
        print(f"Failed to write file: {e}")
        exit(2)
    fname = f"{FIRMWARE_PREFIX}-{ver_major}.{ver_minor}.{ver_patch}.uf2"
    print(f'outputting {fname}...')
    # python ../../uf2conv.py -o ${ProjName}.uf2 -c -f STM32L4 ${ProjName}.hex
    class DotAccessibleObject:
        def __getattr__(self, key): 
            return False
    uf2args = DotAccessibleObject()
    uf2args.output=fname
    uf2args.convert=True
    uf2args.family='STM32L4'
    uf2args.input='../sw/Release/plinkyblack.hex'
    uf2args.base='0x2000' # lol this is wrong, but luckily we were using .hex which has addresses. phew.
    # the actual base address is 0x08010000 ie 64k into the flash.
    import uf2conv
    uf2conv.uf2conv(uf2args)

    with open("tempbl.bin", "wb") as f1:
        f1.write(bl_content)
    uf2args.input='tempbl.bin'
    uf2args.base='0x20008000'
    uf2args.output=f"tempbl.uf2"
    uf2conv.uf2conv(uf2args)
    # concatenate the two uf2 files tempbl.uf2 and fname
    with open(f"BOOTLOAD.UF2", "wb") as f1:
        with open("tempbl.uf2", "rb") as f2:
            f1.write(f2.read())
        with open(fname, "rb") as f2:
            f1.write(f2.read())
    # delete temp files
    import os
    os.remove("tempbl.bin")
    os.remove("tempbl.uf2")
    print("wrote a combiled firmware and bootloader package to BOOTLOAD.UF2")
    checksum=0
    # interpret bl_content as a list of 32-bit words
    for i in range(0, len(bl_content), 4):
        checksum = (checksum * 23 + int.from_bytes(bl_content[i:i+4], 'little')) & 0xffffffff
    print(f"bootloader checksum: 0x{checksum:08x} - this should be copied to GOLDEN_CHECKSUM in config.h")

if __name__ == "__main__":
    main()
