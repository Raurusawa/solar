#!/usr/bin/env python3
"""
Recursively check DLL dependencies and identify which ones are missing.
System DLLs are those that are part of Windows and don't need to be distributed.
"""
import subprocess
import sys
import os

# System DLLs that are part of Windows - don't need to distribute
SYSTEM_DLLS = {
    'KERNEL32.dll', 'USER32.dll', 'GDI32.dll', 'ADVAPI32.dll',
    'SHELL32.dll', 'msvcrt.dll', 'ole32.dll', 'WS2_32.dll',
    'DWrite.dll', 'RPCRT4.dll', 'USP10.dll', 'usp10.dll',
    'OPENGL32.dll', 'opengl32.dll', 'dinput8.dll', 'dwmapi.dll',
    'shcore.dll', 'ntdll.dll', 'EGL.dll', 'GLESv1_CM.dll',
    'GLESv2.dll', 'OSMesa.dll', 'vulkan-1.dll',
    'xinput1_1.dll', 'xinput1_2.dll', 'xinput1_3.dll', 'xinput1_4.dll', 'xinput9_1_0.dll',
    'api-ms-win-crt-convert-l1-1-0.dll', 'api-ms-win-crt-environment-l1-1-0.dll',
    'api-ms-win-crt-filesystem-l1-1-0.dll', 'api-ms-win-crt-heap-l1-1-0.dll',
    'api-ms-win-crt-locale-l1-1-0.dll', 'api-ms-win-crt-math-l1-1-0.dll',
    'api-ms-win-crt-private-l1-1-0.dll', 'api-ms-win-crt-runtime-l1-1-0.dll',
    'api-ms-win-crt-stdio-l1-1-0.dll', 'api-ms-win-crt-string-l1-1-0.dll',
    'api-ms-win-crt-utility-l1-1-0.dll',
}

def get_dll_imports(dll_path):
    """Get list of DLLs imported by a given DLL using strings command."""
    try:
        result = subprocess.run(['strings', dll_path], capture_output=True, text=True)
        imports = set()
        for line in result.stdout.split('\n'):
            line = line.strip()
            # Only match exact DLL names (not error messages)
            if line.lower().endswith('.dll') and ' ' not in line and len(line) < 50:
                imports.add(line)
        return imports
    except Exception as e:
        print(f"Error reading {dll_path}: {e}", file=sys.stderr)
        return set()

def check_dependencies(release_dir):
    """Recursively check all DLL dependencies in the release directory."""
    # Get all DLLs in the release directory
    dlls = [f for f in os.listdir(release_dir) if f.lower().endswith('.dll')]
    
    print(f"Found {len(dlls)} DLLs in {release_dir}")
    print("=" * 60)
    
    # Track all dependencies
    all_deps = set()
    missing = set()
    
    # Check each DLL
    for dll in dlls:
        dll_path = os.path.join(release_dir, dll)
        imports = get_dll_imports(dll_path)
        
        for imp in imports:
            if imp in SYSTEM_DLLS:
                continue  # Skip system DLLs
            
            # Check if this DLL is in the release directory
            if imp not in all_deps:
                all_deps.add(imp)
                if imp not in dlls:
                    missing.add(imp)
    
    # Print results
    if missing:
        print("\nMISSING non-system DLLs:")
        print("-" * 60)
        for dll in sorted(missing):
            print(f"  ❌ {dll}")
    else:
        print("\n✅ All non-system DLL dependencies are present!")
    
    print("\n" + "=" * 60)
    print(f"Total non-system DLLs referenced: {len(all_deps)}")
    print(f"Missing: {len(missing)}")
    
    return missing

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python check_dll_deps.py <release_directory>")
        sys.exit(1)
    
    release_dir = sys.argv[1]
    if not os.path.isdir(release_dir):
        print(f"Error: {release_dir} is not a directory")
        sys.exit(1)
    
    missing = check_dependencies(release_dir)
    sys.exit(0 if not missing else 1)
