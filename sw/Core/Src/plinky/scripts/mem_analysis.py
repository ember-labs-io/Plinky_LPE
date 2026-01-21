#!/usr/bin/env python3
"""Parse RAM and FLASH usage from linker map file."""

import re
from collections import defaultdict
import os

MAP_FILE = 'sw/build/RELEASE/plinkyblack.map'
OUTPUT_FILE = 'sw/Core/Src/plinky/scripts/mem_analysis.md'

def get_ram_section_sizes():
    """Get the sizes of all RAM sections from the map file."""
    with open(MAP_FILE, 'r') as f:
        content = f.read()

    sections = {}

    # Find .data section
    data_header = re.search(r'^\.data\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)', content, re.MULTILINE)
    if data_header:
        sections['data'] = int(data_header.group(2), 16)

    # Find .bss section
    bss_header = re.search(r'^\.bss\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)', content, re.MULTILINE)
    if bss_header:
        sections['bss'] = int(bss_header.group(2), 16)

    # Find ._user_heap_stack section
    heap_stack_header = re.search(r'^\._user_heap_stack\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)', content, re.MULTILINE)
    if heap_stack_header:
        sections['heap_stack'] = int(heap_stack_header.group(2), 16)

    return sections

def get_flash_section_sizes():
    """Get the sizes of all FLASH sections from the map file."""
    with open(MAP_FILE, 'r') as f:
        content = f.read()

    sections = {}

    # Common flash sections
    flash_section_names = [
        '.isr_vector',
        '.text',
        '.rodata',
        '.ARM.extab',
        '.ARM',
        '.preinit_array',
        '.init_array',
        '.fini_array'
    ]

    for section_name in flash_section_names:
        pattern = rf'^{re.escape(section_name)}\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)'
        match = re.search(pattern, content, re.MULTILINE)
        if match:
            sections[section_name] = int(match.group(2), 16)

    return sections

def parse_section(content, section_name):
    """Parse a specific section from the map file."""
    # Find section
    section_match = re.search(rf'^{re.escape(section_name)}\s+0x[0-9a-f]+\s+0x[0-9a-f]+.*?(?=^\S|\Z)',
                              content, re.MULTILINE | re.DOTALL)

    if not section_match:
        print(f"  WARNING: Section '{section_name}' not found in map file")
        return defaultdict(list)

    section_content = section_match.group(0)
    lines = section_content.split('\n')
    print(f"  Section '{section_name}' found, {len(lines)} lines to parse")

    # Data structure: module -> list of (variable, size, address)
    modules = defaultdict(list)

    vars_found = 0
    i = 0
    while i < len(lines):
        line = lines[i]

        # Look for variable declarations (start with section prefix like .bss. or .data.)
        # Extract the section prefix dynamically
        var_match = re.match(r'\s*(\.(?:bss|data)\.\S+)', line)
        if var_match:
            var_name_raw = var_match.group(1)
            # Remove section prefix (.bss. or .data.)
            var_name = re.sub(r'^\.(?:bss|data)\.', '', var_name_raw)

            # Look ahead for address and size info
            # Could be on same line or next line
            found = False

            # Check same line first
            same_line_match = re.search(r'\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+sw/build/RELEASE/Core/Src/(.+?)\.o', line)
            if same_line_match:
                addr, size_hex, module = same_line_match.groups()
                size = int(size_hex, 16)
                if size > 0:
                    modules[module].append({
                        'symbol': var_name,
                        'size': size,
                        'size_hex': size_hex,
                        'address': addr
                    })
                    vars_found += 1
                found = True

            # Check next line if not found
            if not found and i + 1 < len(lines):
                next_line = lines[i + 1]
                alloc_match = re.match(r'\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+sw/build/RELEASE/Core/Src/(.+?)\.o', next_line)
                if alloc_match:
                    addr, size_hex, module = alloc_match.groups()
                    size = int(size_hex, 16)
                    if size > 0:
                        modules[module].append({
                            'symbol': var_name,
                            'size': size,
                            'size_hex': size_hex,
                            'address': addr
                        })
                        vars_found += 1

        i += 1

    print(f"  Parsed {vars_found} variables from '{section_name}' section")
    return modules

def parse_rodata_section(content):
    """Parse .rodata section from the map file."""
    # Find section
    section_match = re.search(r'^\.rodata\s+0x[0-9a-f]+\s+0x[0-9a-f]+.*?(?=^\.\w+\s+0x|\Z)',
                              content, re.MULTILINE | re.DOTALL)

    if not section_match:
        print("  WARNING: .rodata section not found in map file")
        return defaultdict(list)

    section_content = section_match.group(0)
    lines = section_content.split('\n')
    print(f"  Section '.rodata' found, {len(lines)} lines to parse")

    # Data structure: module -> list of (symbol, size, address)
    modules = defaultdict(list)

    symbols_found = 0
    i = 0
    while i < len(lines):
        line = lines[i]

        # Format 1: .rodata subsection with address/size/file on same line
        same_line_match = re.match(r'\s*(\.rodata[\.\w]*)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.+?\.o)', line)
        if same_line_match:
            symbol_name_raw = same_line_match.group(1)
            addr = same_line_match.group(2)
            size_hex = same_line_match.group(3)
            obj_file = same_line_match.group(4)

            size = int(size_hex, 16)
            if size > 0:
                # Extract module name from path
                module = extract_module_name(obj_file)

                # Remove .rodata prefix for cleaner names
                symbol_name = re.sub(r'^\.rodata\.?', '', symbol_name_raw)
                if not symbol_name:
                    symbol_name = '(unnamed)'

                modules[module].append({
                    'symbol': symbol_name,
                    'size': size,
                    'size_hex': size_hex,
                    'address': addr
                })
                symbols_found += 1
        else:
            # Format 2: .rodata subsection on one line, address/size/file on next line
            subsection_match = re.match(r'\s*(\.rodata[\.\w]+)\s*$', line)
            if subsection_match and i + 1 < len(lines):
                symbol_name_raw = subsection_match.group(1)
                next_line = lines[i + 1]

                # Look for address, size, and object file on next line
                next_match = re.match(r'\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.+?\.o)', next_line)
                if next_match:
                    addr = next_match.group(1)
                    size_hex = next_match.group(2)
                    obj_file = next_match.group(3)

                    size = int(size_hex, 16)
                    if size > 0:
                        # Extract module name from path
                        module = extract_module_name(obj_file)

                        # Remove .rodata prefix for cleaner names
                        symbol_name = re.sub(r'^\.rodata\.?', '', symbol_name_raw)
                        if not symbol_name:
                            symbol_name = '(unnamed)'

                        modules[module].append({
                            'symbol': symbol_name,
                            'size': size,
                            'size_hex': size_hex,
                            'address': addr
                        })
                        symbols_found += 1

        i += 1

    print(f"  Parsed {symbols_found} symbols from '.rodata' section")
    return modules

def module_to_anchor(module_name):
    """Convert module name to markdown anchor link."""
    # GitHub markdown anchor format: lowercase, spaces->hyphens, keep alphanumeric/hyphen/underscore
    import string
    anchor = module_name.lower()
    # Keep only alphanumeric, hyphens, underscores, and spaces
    allowed = string.ascii_lowercase + string.digits + '-_'
    anchor = ''.join(c if c in allowed else '' for c in anchor)
    return anchor

def extract_module_name(obj_file_path):
    """Extract module name from object file path."""
    # Remove .o extension
    path = obj_file_path.replace('.o', '')

    # Extract meaningful path after build directory
    # Examples:
    # sw/build/RELEASE/Core/Src/plinky/hardware/adc_dac.o -> plinky/hardware/adc_dac
    # sw/build/RELEASE/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_gpio.o -> Drivers/.../stm32l4xx_hal_gpio

    if '/Core/Src/' in path:
        # Extract everything after Core/Src/
        return path.split('/Core/Src/')[-1]
    elif '/Drivers/' in path:
        # For drivers, get the driver name and file
        parts = path.split('/')
        if 'Drivers' in parts:
            idx = parts.index('Drivers')
            # Get driver directory and filename
            if idx + 1 < len(parts):
                driver_name = parts[idx + 1]
                filename = parts[-1]
                return f"Drivers/{driver_name}/{filename}"

    # Fallback: just return the filename
    return path.split('/')[-1]

def parse_map_file():
    """Parse all RAM and FLASH sections from the map file."""
    print(f"Opening map file: {MAP_FILE}")
    try:
        with open(MAP_FILE, 'r') as f:
            content = f.read()
        print(f"Map file read successfully, {len(content)} bytes")
    except FileNotFoundError:
        print(f"ERROR: Map file not found: {MAP_FILE}")
        return defaultdict(list), defaultdict(list)

    # Parse RAM sections (.data and .bss)
    print("Parsing .data section...")
    data_modules = parse_section(content, '.data')
    print(f"Found {len(data_modules)} modules in .data section")

    print("Parsing .bss section...")
    bss_modules = parse_section(content, '.bss')
    print(f"Found {len(bss_modules)} modules in .bss section")

    # Merge RAM sections
    ram_modules = defaultdict(list)

    for module, symbols in data_modules.items():
        ram_modules[module].extend(symbols)

    for module, symbols in bss_modules.items():
        ram_modules[module].extend(symbols)

    print(f"Total unique RAM modules: {len(ram_modules)}")

    # Parse FLASH sections (.rodata)
    print("Parsing .rodata section...")
    rodata_modules = parse_rodata_section(content)
    print(f"Found {len(rodata_modules)} modules in .rodata section")

    return ram_modules, rodata_modules

def add_padding_to_modules(modules):
    """Add inter-variable alignment padding to variable sizes."""
    # Get section info
    ram_sections = get_ram_section_sizes()

    # Define section boundaries
    data_start = 0x20000000
    data_end = data_start + ram_sections.get('data', 0)
    bss_start = data_end
    bss_end = bss_start + ram_sections.get('bss', 0)

    # Collect all symbols with their module info and reference to original
    all_symbols = []
    for module, symbols in modules.items():
        for symbol in symbols:
            addr = int(symbol['address'], 16)
            all_symbols.append({
                'module': module,
                'symbol_ref': symbol,  # reference to original dict
                'address': addr,
                'size': symbol['size']
            })

    # Sort by address
    all_symbols.sort(key=lambda x: x['address'])

    # Find gaps and update symbol sizes/names
    for i in range(len(all_symbols) - 1):
        current = all_symbols[i]
        next_sym = all_symbols[i + 1]

        current_end = current['address'] + current['size']
        gap = next_sym['address'] - current_end

        # Only add padding if gap exists and both symbols are in same section
        if gap > 0:
            # Check if they're in the same section
            current_in_data = data_start <= current['address'] < data_end
            next_in_data = data_start <= next_sym['address'] < data_end
            current_in_bss = bss_start <= current['address'] < bss_end
            next_in_bss = bss_start <= next_sym['address'] < bss_end

            # Only add padding if in same section
            if (current_in_data and next_in_data) or (current_in_bss and next_in_bss):
                # Update the original symbol dict
                sym_ref = current['symbol_ref']
                sym_ref['symbol'] = f"{sym_ref['symbol']} ({gap} padding)"
                sym_ref['size'] += gap

def print_report(ram_modules, rodata_modules):
    """Print formatted report of RAM and FLASH usage by module."""

    # Get actual section sizes from map file
    ram_sections = get_ram_section_sizes()
    flash_sections = get_flash_section_sizes()

    # Separate modules by section (.data vs .bss)
    data_by_module = defaultdict(lambda: {'symbols': [], 'total': 0})
    bss_by_module = defaultdict(lambda: {'symbols': [], 'total': 0})

    for module, symbols in ram_modules.items():
        for symbol in symbols:
            addr = int(symbol['address'], 16)
            # .data starts at 0x20000000, .bss starts after
            # Check the actual section by address range
            if 0x20000000 <= addr < 0x20000000 + ram_sections.get('data', 0):
                data_by_module[module]['symbols'].append(symbol)
                data_by_module[module]['total'] += symbol['size']
            else:
                bss_by_module[module]['symbols'].append(symbol)
                bss_by_module[module]['total'] += symbol['size']

    # Calculate totals per module (combined)
    module_totals = []
    for module, symbols in ram_modules.items():
        total = sum(s['size'] for s in symbols)
        module_totals.append((module, total, symbols))

    # Sort by total size descending
    module_totals.sort(key=lambda x: x[1], reverse=True)

    # Calculate grand total from parsed data
    grand_total = sum(total for _, total, _ in module_totals)

    # Calculate .data and .bss totals from parsed data
    parsed_data_total = sum(m['total'] for m in data_by_module.values())
    parsed_bss_total = sum(m['total'] for m in bss_by_module.values())

    # Print summary
    print("# Complete Memory Usage Report - RAM and FLASH Sections\n")

    print("## Quick Navigation\n")
    print("- [.bss Section Summary](#summary-by-module)")
    print("- [.data Section Summary](#summary-by-module-1)")
    print("- [.rodata Section Summary](#summary-by-module-2)")
    print()

    print("---\n")
    print("## RAM Section Overview\n")
    print("| Section | Actual Size | Parsed Size | Discrepancy | % of Total |")
    print("|---------|-------------|-------------|-------------|------------|")

    total_ram_used = 0
    total_discrepancy = 0

    if 'data' in ram_sections:
        data_size = ram_sections['data']
        data_disc = data_size - parsed_data_total
        total_discrepancy += data_disc
        print(f"| .data (initialized) | {data_size:,} bytes | {parsed_data_total:,} bytes | {data_disc:,} bytes | {data_size/328.68:.1f}% |")
        total_ram_used += data_size

    if 'bss' in ram_sections:
        bss_size = ram_sections['bss']
        bss_disc = bss_size - parsed_bss_total
        total_discrepancy += bss_disc
        print(f"| .bss (uninitialized) | {bss_size:,} bytes | {parsed_bss_total:,} bytes | {bss_disc:,} bytes | {bss_size/328.68:.1f}% |")
        total_ram_used += bss_size

    if 'heap_stack' in ram_sections:
        hs_size = ram_sections['heap_stack']
        print(f"| ._user_heap_stack | {hs_size:,} bytes | N/A | N/A | {hs_size/328.68:.1f}% |")
        total_ram_used += hs_size

    print(f"\n**Total RAM Used:** {total_ram_used:,} bytes ({total_ram_used/1024:.2f} KB)")
    print(f"**Total Parsed:** {grand_total:,} bytes ({grand_total/1024:.2f} KB)")
    print(f"**Total Discrepancy:** {total_discrepancy:,} bytes ({total_discrepancy/1024:.2f} KB) - padding/alignment/library")
    print(f"\n**Available RAM:** 32,768 bytes (32 KB)")
    overflow = total_ram_used - 32768
    if overflow > 0:
        print(f"❌ **OVERFLOW:** {overflow:,} bytes ({overflow/1024:.2f} KB) - **BUILD WILL FAIL**")
    else:
        print(f"✓ **Free RAM:** {-overflow:,} bytes ({-overflow/1024:.2f} KB)")

    # Print detailed breakdown - BSS first, then DATA
    print("\n---\n")
    print("## .bss Section (Uninitialized Data)\n")

    # Summary table for BSS modules
    print("### Summary by Module\n")
    print("| **Module** | **.bss (bytes)** | **% of 32KB RAM** | **Num Variables** |")
    print("|--------|--------------|---------------|---------------|")

    bss_module_totals = [(m, bss_by_module[m]['total'], bss_by_module[m]['symbols'])
                         for m, _, _ in module_totals if bss_by_module[m]['total'] > 0]
    bss_module_totals.sort(key=lambda x: x[1], reverse=True)

    total_bss_vars = 0
    for module, bss_total, bss_symbols in bss_module_totals:
        pct_ram = (bss_total / 32768) * 100
        anchor = module_to_anchor(module)
        print(f"| [`{module}`](#{anchor}) | {bss_total:,} | {pct_ram:.1f}% | {len(bss_symbols)} |")
        total_bss_vars += len(bss_symbols)

    total_bss_pct = (parsed_bss_total / 32768) * 100
    print(f"| **TOTAL** | **{parsed_bss_total:,}** | **{total_bss_pct:.1f}%** | **{total_bss_vars}** |")

    # Detailed breakdown per module for BSS
    print("\n### Per-Module Breakdown\n")
    for module, bss_total, bss_symbols in bss_module_totals:
        bss_symbols_sorted = sorted(bss_symbols, key=lambda x: x['size'], reverse=True)

        print(f"#### {module}")
        print(f"\n**Total:** {bss_total:,} bytes ({bss_total/1024:.2f} KB) | **Variables:** {len(bss_symbols)}\n")
        print("| **Variable** | **Size (bytes)** | **% of Module** | **% of 32KB RAM** |")
        print("|----------|--------------|-------------|---------------|")

        for sym in bss_symbols_sorted:
            pct_module = (sym['size'] / bss_total) * 100
            pct_ram = (sym['size'] / 32768) * 100
            print(f"| `{sym['symbol']}` | {sym['size']:,} | {pct_module:.1f}% | {pct_ram:.2f}% |")

        print(f"| **TOTAL** | **{bss_total:,}** | **100.0%** | **{(bss_total/32768)*100:.2f}%** |")
        print()

    # Print detailed breakdown for DATA section
    print("\n---\n")
    print("## .data Section (Initialized Data)\n")

    # Summary table for DATA modules
    print("### Summary by Module\n")
    print("| **Module** | **.data (bytes)** | **% of 32KB RAM** | **Num Variables** |")
    print("|--------|---------------|---------------|---------------|")

    data_module_totals = [(m, data_by_module[m]['total'], data_by_module[m]['symbols'])
                          for m, _, _ in module_totals if data_by_module[m]['total'] > 0]
    data_module_totals.sort(key=lambda x: x[1], reverse=True)

    total_data_vars = 0
    for module, data_total, data_symbols in data_module_totals:
        pct_ram = (data_total / 32768) * 100
        anchor = module_to_anchor(module)
        print(f"| [`{module}`](#{anchor}) | {data_total:,} | {pct_ram:.1f}% | {len(data_symbols)} |")
        total_data_vars += len(data_symbols)

    total_data_pct = (parsed_data_total / 32768) * 100
    print(f"| **TOTAL** | **{parsed_data_total:,}** | **{total_data_pct:.1f}%** | **{total_data_vars}** |")

    # Detailed breakdown per module for DATA
    print("\n### Per-Module Breakdown\n")
    for module, data_total, data_symbols in data_module_totals:
        data_symbols_sorted = sorted(data_symbols, key=lambda x: x['size'], reverse=True)

        print(f"#### {module}")
        print(f"\n**Total:** {data_total:,} bytes ({data_total/1024:.2f} KB) | **Variables:** {len(data_symbols)}\n")
        print("| **Variable** | **Size (bytes)** | **% of Module** | **% of 32KB RAM** |")
        print("|----------|--------------|-------------|---------------|")

        for sym in data_symbols_sorted:
            pct_module = (sym['size'] / data_total) * 100
            pct_ram = (sym['size'] / 32768) * 100
            print(f"| `{sym['symbol']}` | {sym['size']:,} | {pct_module:.1f}% | {pct_ram:.2f}% |")

        print(f"| **TOTAL** | **{data_total:,}** | **100.0%** | **{(data_total/32768)*100:.2f}%** |")
        print()

    # Print FLASH sections overview
    print("\n---\n")
    print("## FLASH Usage Overview\n")

    # Calculate total flash used
    total_flash_used = sum(flash_sections.values())
    flash_total = 412 * 1024  # 412 KB available for firmware

    print("| **Section** | **Size (bytes)** | **% of Total FLASH** |")
    print("|-------------|------------------|----------------------|")

    section_descriptions = {
        '.isr_vector': 'Interrupt vectors / startup code',
        '.text': 'Executable code',
        '.rodata': 'Read-only data (const arrays, strings, lookup tables)',
        '.ARM.extab': 'Exception handling tables',
        '.ARM': 'ARM-specific metadata',
        '.preinit_array': 'Pre-initialization functions',
        '.init_array': 'Initialization functions',
        '.fini_array': 'Finalization functions',
    }

    for section_name in ['.isr_vector', '.text', '.rodata', '.ARM.extab', '.ARM', '.preinit_array', '.init_array', '.fini_array']:
        if section_name in flash_sections:
            size = flash_sections[section_name]
            pct_flash = (size / flash_total) * 100
            desc = section_descriptions.get(section_name, '')
            print(f"| `{section_name}` - {desc} | {size:,} | {pct_flash:.1f}% |")

    print(f"| **TOTAL** | **{total_flash_used:,}** | **{(total_flash_used/flash_total)*100:.1f}%** |")

    remaining_flash = flash_total - total_flash_used
    print(f"\n**Total FLASH Available:** {flash_total:,} bytes ({flash_total/1024:.0f} KB)")
    print(f"**FLASH Used:** {total_flash_used:,} bytes ({total_flash_used/1024:.2f} KB)")
    print(f"**FLASH Remaining:** {remaining_flash:,} bytes ({remaining_flash/1024:.2f} KB)")

    # Print .rodata per-module breakdown
    print("\n---\n")
    print("## .rodata Section (Read-Only Data)\n")

    if rodata_modules:
        # Calculate totals per module
        rodata_module_totals = []
        for module, symbols in rodata_modules.items():
            total = sum(s['size'] for s in symbols)
            rodata_module_totals.append((module, total, symbols))

        # Sort by total size descending
        rodata_module_totals.sort(key=lambda x: x[1], reverse=True)

        # Calculate total
        total_rodata_parsed = sum(total for _, total, _ in rodata_module_totals)
        total_rodata_actual = flash_sections.get('.rodata', 0)

        # Summary table
        print("### Summary by Module\n")
        print("| **Module** | **.rodata (bytes)** | **% of .rodata** | **Num Symbols** |")
        print("|------------|---------------------|------------------|-----------------|")

        for module, rodata_total, rodata_symbols in rodata_module_totals:
            pct_rodata = (rodata_total / total_rodata_actual) * 100 if total_rodata_actual > 0 else 0
            anchor = module_to_anchor(module)
            print(f"| [`{module}`](#{anchor}) | {rodata_total:,} | {pct_rodata:.1f}% | {len(rodata_symbols)} |")

        total_rodata_symbols = sum(len(symbols) for _, _, symbols in rodata_module_totals)
        print(f"| **TOTAL** | **{total_rodata_parsed:,}** | **{(total_rodata_parsed/total_rodata_actual)*100:.1f}%** | **{total_rodata_symbols}** |")

        discrepancy = total_rodata_actual - total_rodata_parsed
        print(f"\n**Actual .rodata size:** {total_rodata_actual:,} bytes")
        print(f"**Parsed .rodata size:** {total_rodata_parsed:,} bytes")
        print(f"**Discrepancy:** {discrepancy:,} bytes (padding/alignment/library)")

        # Detailed breakdown per module
        print("\n### Per-Module Breakdown\n")
        for module, rodata_total, rodata_symbols in rodata_module_totals:
            rodata_symbols_sorted = sorted(rodata_symbols, key=lambda x: x['size'], reverse=True)

            print(f"#### {module}")
            print(f"\n**Total:** {rodata_total:,} bytes ({rodata_total/1024:.2f} KB) | **Symbols:** {len(rodata_symbols)}\n")
            print("| **Symbol** | **Size (bytes)** | **% of Module** | **% of Total FLASH** |")
            print("|------------|------------------|-----------------|----------------------|")

            for sym in rodata_symbols_sorted:
                pct_module = (sym['size'] / rodata_total) * 100
                pct_flash = (sym['size'] / flash_total) * 100
                print(f"| `{sym['symbol']}` | {sym['size']:,} | {pct_module:.1f}% | {pct_flash:.2f}% |")

            print(f"| **TOTAL** | **{rodata_total:,}** | **100.0%** | **{(rodata_total/total_rodata_actual)*100:.2f}%** |")
            print()
    else:
        print("No .rodata symbols found in map file.")
        print()

if __name__ == '__main__':
    import sys

    print("Starting memory usage analysis...")
    ram_modules, rodata_modules = parse_map_file()

    if ram_modules or rodata_modules:
        print(f"SUCCESS: Found {len(ram_modules)} RAM modules and {len(rodata_modules)} FLASH modules to analyze")
        # Add inter-variable padding to RAM modules
        print("Adding padding calculations...")
        add_padding_to_modules(ram_modules)
        # Write report to file
        print(f"Writing report to {OUTPUT_FILE}...")
        original_stdout = sys.stdout
        with open(OUTPUT_FILE, 'w') as f:
            sys.stdout = f
            print_report(ram_modules, rodata_modules)
        sys.stdout = original_stdout
        print(f"Memory analysis written to {OUTPUT_FILE}")
    else:
        print("ERROR: No modules found in map file. Check regex patterns.")
