#!/usr/bin/env python3

import math
import os
import sys

WARNING="""
=== !!! WARNING !!! ==========================================================
This script is going to patch your TF2 client. Connecting to VAC-secured
servers with a modified client can get you VAC banned. It probably won't
happen, but it could. I am not responsible for any damages done to your
computer or Steam account.

Read more: https://help.steampowered.com/en/faqs/view/571A-97DA-70E9-FF74
===========================================================!!! WARNING !!! ===
"""

PATCHES_SO = [
    {
        'bin': os.path.join('tf', 'bin', 'client.so'),
        'patches': [
            # Block call to ClientModeTFNormal::BIsFriendOrPartyMember() in
            # CHudInspectPanel::UserCmd_InspectTarget()
            {
                'sig':  '0F 84 ? ? ? ? 8B 07 89 55 ? 89 4D ? 89 3C 24 FF 50',
                'data': [ 0x90 ] * 6    # NOP (x6)
            }
        ]
    },
]

PATCHES_DLL = [
    {
        'bin': os.path.join('tf', 'bin', 'client.dll'),
        'patches': [
            # Block call to ClientModeTFNormal::BIsFriendOrPartyMember() in
            # CHudInspectPanel::UserCmd_InspectTarget()
            {
                'sig':  '75 ? 8B 0D ? ? ? ? 68 ? ? ? ? 8B 01 FF 50 ? 5E 5F',
                'data': [ 0xEB ]        # JMP
            }
        ]
    },
    {
        'bin': os.path.join('bin', 'engine.dll'),
        'patches': [
            # Block call to Host_DisallowSecureServers() in ClientDLL_Load()
            {
                'sig':  'A2 ? ? ? ? E8 ? ? ? ? 83 C4 10',
                'data': [ 0x90 ] * 5    # NOP (x5)
            },
            # Block call to Host_DisallowSecureServers() in Host_Init()
            {
                'sig':  '20 05 ? ? ? ? EB',
                'data': [ 0x90 ] * 6    # NOP (x6)
            }
        ]
    },
]

PATCHES = PATCHES_DLL if os.name == 'nt' else PATCHES_SO

def build_sig(sigstr):
    sig = []
    for bytestr in sigstr.split():
        if bytestr[0] == '?':
            sig.append(None)
        else:
            sig.append(int(bytestr, 16))
    return sig

def find_sig(data, sig):
    for i, _ in enumerate(data):
        found = True
        for j, s in enumerate(sig):
            if s is not None and data[i + j] != s:
                found = False
                break
        if found:
            return i
    return 0;

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f'Usage: {sys.argv[0]} <tf2-path>')
        exit(1)

    print(WARNING)
    print('Press Y to continue or any other key to quit: ')
    if str(input()).lower().strip() != 'y':
        exit(1)

    # Process and apply patches
    for i, lib in enumerate(PATCHES):
        print(f'Processing {lib["bin"]}')

        # Load file
        fpath = os.path.join(sys.argv[1], lib['bin'])
        file = None
        print('    Loading file...           ', end='', flush=True)
        with open(fpath, 'rb') as f:
            file = bytearray(f.read())
        assert file
        print(f'Done ({math.floor(len(file) / 1024 / 1024)}MB)')

        # Apply patches
        npatch = len(lib['patches'])
        for j, patch in enumerate(lib['patches']):
            print(f'    Applying patch {j + 1}/{npatch}...     ',
                    end='', flush=True)

            # Find patch target
            dest = find_sig(file, build_sig(patch['sig']))
            assert dest != 0

            # Apply patch
            for k, b in enumerate(patch['data']):
                file[dest + k] = b

            print('Done')

        # Write back file
        print('    Writing file...           ', end='', flush=True)
        with open(fpath, 'wb') as f:
            f.write(file)
        print('Done')

    print('Have a nice day')
