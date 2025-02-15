//
//  ibootpatchfinder64_base.cpp
//  liboffsetfinder64
//
//  Created by tihmstar on 28.09.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include <libgeneral/macros.h>

#include "ibootpatchfinder64_base.hpp"
#include "all_liboffsetfinder.hpp"
#include "OFexception.hpp"

using namespace std;
using namespace tihmstar::offsetfinder64;
using namespace tihmstar::libinsn;

#define IBOOT_STAGE_STR_OFFSET 0x200
#define IBOOT_MODE_STR_OFFSET 0x240
#define IBOOT_VERS_STR_OFFSET 0x280
#define iBOOT_BASE_OFFSET 0x318
#define KERNELCACHE_PREP_STRING "__PAGEZERO"
#define ENTERING_RECOVERY_CONSOLE "Entering recovery mode, starting command prompt"
#define DEBUG_ENABLED_DTRE_VAR_STR "debug-enabled"
#define DEFAULT_BOOTARGS_STR "rd=md0 nand-enable-reformat=1 -progress"
#define DEFAULT_BOOTARGS_STR_13 "rd=md0 -progress -restore"
#define DEFAULT_BOOTARGS_STR_OTHER "rd=md0"
#define DEFAULT_BOOTARGS_STR_OTHER1 " -progress"
#define DEFAULT_BOOTARGS_STR_OTHER2 " -restore"
#define CERT_STR "Apple Inc.1"
#define _270ZEROES "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"

ibootpatchfinder64_base::ibootpatchfinder64_base(const char * filename) :
    ibootpatchfinder64(true)
{
    struct stat fs = {0};
    int fd = 0;
    bool didConstructSuccessfully = false;
    cleanup([&]{
        if (fd>0) close(fd);
        if (!didConstructSuccessfully) {
            safeFreeConst(_buf);
        }
    })
    
    assure((fd = open(filename, O_RDONLY)) != -1);
    assure(!fstat(fd, &fs));
    assure((_buf = (uint8_t*)malloc( _bufSize = fs.st_size)));
    assure(read(fd,(void*)_buf,_bufSize)==_bufSize);
    
    assure(_bufSize > 0x1000);
    
    assure(!strncmp((char*)&_buf[IBOOT_VERS_STR_OFFSET], "iBoot", sizeof("iBoot")-1));
    retassure(_vers = atoi((char*)&_buf[IBOOT_VERS_STR_OFFSET+6]), "No iBoot version found!");
    debug("_vers: %d\n", _vers);
    if(_vers < 3000) {
        debug("1337: 1\n");
        stage1 = !strncmp((char *) &_buf[IBOOT_STAGE_STR_OFFSET], "iBootStage1", sizeof("iBootStage1") - 1);
        stage2 = !strncmp((char *) &_buf[IBOOT_STAGE_STR_OFFSET], "iBootStage2", sizeof("iBootStage2") - 1);
    } else {
        debug("1337: 2\n");
        stage1 = !strncmp((char *) &_buf[IBOOT_STAGE_STR_OFFSET], "iBSS", sizeof("iBSS") - 1);
        stage2 = !strncmp((char *) &_buf[IBOOT_STAGE_STR_OFFSET], "iBEC", sizeof("iBEC") - 1);
    }
    debug("1337: 3\n");
    dev = !strncmp((char*)&_buf[IBOOT_MODE_STR_OFFSET], "DEVELOPMENT", sizeof("DEVELOPMENT")-1);
    debug("mode=%s\n", dev ? "DEVELOPMENT" : "RELEASE");
    retassure(*(uint32_t*)&_buf[0] == 0x90000000 || *(uint32_t*)&_buf[4] == 0x90000000, "invalid magic");
    _entrypoint = _base = (loc_t)*(uint64_t*)&_buf[iBOOT_BASE_OFFSET];
    debug("iBoot base at=0x%016llx\n", _base);
    if(!_vmem) {
        _vmem = new vmem({{_buf, _bufSize, _base, vsegment::vmprot::kVMPROTREAD | vsegment::vmprot::kVMPROTWRITE |
                                                  vsegment::vmprot::kVMPROTEXEC}});
    }
    std::string _vers_str = std::string((char*)&_buf[IBOOT_VERS_STR_OFFSET+6]);
    for(int i = 0; i < 5; i++) {
        std::size_t pos = _vers_str.find('.');
        if(pos != std::string::npos) {
            _vers_str = _vers_str.substr(pos + 1, _vers_str.size() - 1);
            _vers_arr[i] = atoi((char*)_vers_str.c_str());
        }
    }
    debug("iBoot-%d inputted\n", _vers);

    if(!stage1) {
        loc_t platform_name_str_loc = _vmem->memstr("platform-name");
        debug("platform_name_str_loc: %p\n", platform_name_str_loc);
        loc_t platform_name_str_xref;
        assure(platform_name_str_xref = find_literal_ref(platform_name_str_loc));
        debug("platform_name_str_xref: %p\n", platform_name_str_xref);
        vmem platform_name_str_mem(*_vmem,platform_name_str_xref);
        while(++platform_name_str_mem != insn::adr) continue;
        loc_t chipid_str = platform_name_str_mem().imm();
        _chipid = std::atoi((char*)&_buf[chipid_str + 1 - _base]);
        debug("iBoot chipid = %d\n", _chipid);
    }
    
    didConstructSuccessfully = true;
}

ibootpatchfinder64_base::ibootpatchfinder64_base(const void *buffer, size_t bufSize, bool takeOwnership)
:    ibootpatchfinder64(takeOwnership)
{
    _bufSize = bufSize;
    _buf = (uint8_t*)buffer;
    assure(_bufSize > 0x1000);
    
    assure(!strncmp((char*)&_buf[IBOOT_VERS_STR_OFFSET], "iBoot", sizeof("iBoot")-1));
    retassure(_vers = atoi((char*)&_buf[IBOOT_VERS_STR_OFFSET+6]), "No iBoot version found!");
    if(_vers < 3000) {
        stage1 = !strncmp((char *) &_buf[IBOOT_STAGE_STR_OFFSET], "iBSS", sizeof("iBSS") - 1);
        stage2 = !strncmp((char *) &_buf[IBOOT_STAGE_STR_OFFSET], "iBEC", sizeof("iBEC") - 1);
    } else {
        stage1 = !strncmp((char *) &_buf[IBOOT_STAGE_STR_OFFSET], "iBootStage1", sizeof("iBootStage1") - 1);
        stage2 = !strncmp((char *) &_buf[IBOOT_STAGE_STR_OFFSET], "iBootStage2", sizeof("iBootStage2") - 1);
    }
    dev = !strncmp((char*)&_buf[IBOOT_MODE_STR_OFFSET], "DEVELOPMENT", sizeof("DEVELOPMENT")-1);
    debug("mode=%s\n", dev ? "DEVELOPMENT" : "RELEASE");
    retassure(*(uint32_t*)&_buf[0] == 0x90000000, "invalid magic");
    _entrypoint = _base = (loc_t)*(uint64_t*)&_buf[iBOOT_BASE_OFFSET];
    debug("iBoot base at=0x%016llx\n", _base);
    _vmem = new vmem({{_buf,_bufSize,_base, vsegment::vmprot::kVMPROTREAD | vsegment::vmprot::kVMPROTWRITE | vsegment::vmprot::kVMPROTEXEC}});
    std::string _vers_str = std::string((char*)&_buf[IBOOT_VERS_STR_OFFSET+6]);
    for(int i = 0; i < 5; i++) {
        std::size_t pos = _vers_str.find('.');
        if(pos != std::string::npos) {
            _vers_str = _vers_str.substr(pos + 1, _vers_str.size() - 1);
            _vers_arr[i] = atoi((char*)_vers_str.c_str());
        }
    }
    debug("iBoot-%d inputted\n", _vers);

    if(!stage1) {
        loc_t platform_name_str_loc = _vmem->memstr("platform-name");
        debug("platform_name_str_loc: %p\n", platform_name_str_loc);
        loc_t platform_name_str_xref;
        assure(platform_name_str_xref = find_literal_ref(platform_name_str_loc));
        debug("platform_name_str_xref: %p\n", platform_name_str_xref);
        vmem platform_name_str_mem(*_vmem,platform_name_str_xref);
        while(++platform_name_str_mem != insn::adr) continue;
        loc_t chipid_str = platform_name_str_mem().imm();
        _chipid = std::atoi((char*)&_buf[chipid_str + 1 - _base]);
        debug("iBoot chipid = %d\n", _chipid);
    }
}

ibootpatchfinder64_base::~ibootpatchfinder64_base(){
    //
}

bool ibootpatchfinder64_base::has_kernel_load(){
    try {
        return (bool) (_vmem->memstr(KERNELCACHE_PREP_STRING) != 0);
    } catch (...) {
        return 0;
    }
}

bool ibootpatchfinder64_base::has_recovery_console(){
    try {
        return (bool) (_vmem->memstr(ENTERING_RECOVERY_CONSOLE) != 0);
    } catch (...) {
        return 0;
    }
}

std::vector<patch> ibootpatchfinder64_base::get_sigcheck_patch(){
    std::vector<patch> patches;
    loc_t img4decodemanifestexists = 0x0;
    bool isnotptr = false;
    bool isadrl = false;
    if(_vers == 5540 && _vers_arr[0] >= 100 || _vers > 5540) {
        debug("get_sigcheck_patch: iOS 13.4 or later(iBoot-%d.%d) detected.",_vers, _vers_arr[0]);
        img4decodemanifestexists = _vmem->memmem("\xE8\x03\x00\xAA\xC0\x00\x80\x52\xE8\x00\x00\xB4", 12);
    } else if(_vers == 5540 && _vers_arr[0] <= 100 || _vers <= 5540 && _vers >= 3406) {
        debug("get_sigcheck_patch: iOS 13.3 or lower(iBoot-%d.%d) detected.",_vers, _vers_arr[0]);
        img4decodemanifestexists = _vmem->memmem("\xE8\x03\x00\xAA\xE0\x07\x1F\x32\xE8\x00\x00\xB4", 12);
    } else if(_vers < 3406) {
        if(_vers <= 1940) {
            debug("get_sigcheck_patch: iOS 7.1.2 or lower(iBoot-%d.%d) detected.",_vers, _vers_arr[0]);
            isadrl = true;
        } else {
            debug("get_sigcheck_patch: iOS 9.3.6 or lower(iBoot-%d.%d) detected.",_vers, _vers_arr[0]);
        }
        isnotptr = true;
        img4decodemanifestexists = _vmem->memmem("\xE8\x07\x1F\x32\xE0\x00\x00\xB4\xC1\x00\x00\xB4", 12);
    } else {
        reterror("unknown or unsupported iboot version");
    }
    debug("img4decodemanifestexists=%p",img4decodemanifestexists);
    assure(img4decodemanifestexists);

    loc_t img4decodemanifestexistsref = find_call_ref(img4decodemanifestexists);
    debug("img4decodemanifestexistsref=%p",img4decodemanifestexistsref);
    assure(img4decodemanifestexistsref);

    vmem iter(*_vmem,img4decodemanifestexistsref);
    vmem iter2(*_vmem,img4decodemanifestexistsref);

    if(isadrl) {
        while(++iter != insn::ldr);
        ++iter;
        if((uint8_t)iter().rd() != 2) {
            while(++iter2 != insn::ldr);
            assure((uint8_t)iter().rd() == 2);
        }
    } else {
        while(++iter != insn::adr);
        if((uint8_t)iter().rd() != 2) {
            while(++iter2 != insn::adr);
            assure((uint8_t)iter().rd() == 2);
        }
    }
    loc_t img4interposercallbackptr = iter().imm();
    debug("img4interposercallbackptr=%p",img4interposercallbackptr);
    assure(img4interposercallbackptr);

    loc_t img4interposercallback = (isnotptr == true) ? img4interposercallbackptr : _vmem->deref(img4interposercallbackptr);
    debug("img4interposercallback=%p",img4interposercallback);
    assure(img4interposercallback);
    if(isnotptr) {
        patches.push_back({img4interposercallback,"\x00\x00\x80\xD2\xC0\x03\x5F\xD6" /*mov x0, 0: ret*/, 8});
        return patches;
    }
    
    vmem iter3(*_vmem,img4interposercallback);
    while(++iter3 != insn::ret);
    loc_t img4interposercallbackret = iter3().pc();
    assure(img4interposercallbackret);
    debug("img4interposercallbackret=%p",img4interposercallbackret);
    patches.push_back({img4interposercallbackret,"\x00\x00\x80\xD2" /*mov x0, 0*/,4});
    patches.push_back({img4interposercallbackret + 4,"\xC0\x03\x5F\xD6" /*ret*/,4});
    if((isnotptr && stage2) || !isnotptr) {
        if(isnotptr) {
            loc_t cpro_jump = find_literal_ref(img4interposercallbackret + 4);
            assure(cpro_jump);
            debug("cpro_jump=%p", cpro_jump);
            patches.push_back({cpro_jump, "\xD5\x03\x20\x1F" /*nop*/, 4});
        }
        ++iter3;
        while (++iter3 != insn::ret);
        loc_t img4interposercallbackret2 = iter3().pc();
        assure(img4interposercallbackret2);
        debug("img4interposercallbackret2=%p", img4interposercallbackret2);
        patches.push_back({img4interposercallbackret2 - 4, "\x00\x00\x80\xD2" /*mov x0, 0*/, 4});
    } else {
        loc_t cpro_jump = find_literal_ref(img4interposercallbackret + 4);
        assure(cpro_jump);
        debug("cpro_jump=%p", cpro_jump);
        patches.push_back({cpro_jump, "\xD5\x03\x20\x1F" /*nop*/, 4});
    }
    return patches;
}

std::vector<patch> ibootpatchfinder64_base::get_demotion_patch(){
    std::vector<patch> patches;
    /* always production patch*/
    for (uint64_t demoteReg : {0x3F500000UL,0x3F500000UL,0x3F500000UL,0x481BC000UL,0x481BC000UL,0x20E02A000UL,0x2102BC000UL,0x2102BC000UL,0x2352BC000UL}) {
        loc_t demoteRef = find_literal_ref(demoteReg);
        if (demoteRef) {
            vmem iter(*_vmem,demoteRef);

            while (++iter != insn::and_);
            assure((uint32_t)iter().imm() == 1);
            demoteRef = iter;
            debug("demoteRef=%p\n",demoteRef);
            patches.push_back({demoteRef,"\x20\x00\x80\xD2" /*mov x0, 0*/,4});
        }
    }
    return patches;
}
std::vector<patch> ibootpatchfinder64_base::get_boot_arg_patch(const char *bootargs) {
    std::vector <patch> patches;
    if (!bootargs)
        return patches;
    loc_t default_boot_args_str_loc = 0;
    loc_t default_boot_args_xref = 0;
    int default_boot_args_len = 0;
    bool _7429_0 = (_vers >= 7429 && _vers_arr[0] >= 0);
    bool _6723_100 = ((_vers == 6723 && _vers_arr[0] >= 100) || (_vers > 6723)) && !_7429_0;
    bool _10151_0 = (_vers >= 10151 && _vers_arr[0] >= 0);

    try {
        default_boot_args_str_loc = _vmem->memstr(DEFAULT_BOOTARGS_STR);
        default_boot_args_len = strlen(DEFAULT_BOOTARGS_STR);
    } catch (...) {
        try {
            debug("DEFAULT_BOOTARGS_STR not found, trying fallback to DEFAULT_BOOTARGS_STR_13\n");
            default_boot_args_str_loc = _vmem->memstr(DEFAULT_BOOTARGS_STR_13);
            default_boot_args_len = strlen(DEFAULT_BOOTARGS_STR_13);
        } catch (...) {
            debug("DEFAULT_BOOTARGS_STR_13 not found, trying fallback to DEFAULT_BOOTARGS_STR_OTHER\n");
            default_boot_args_str_loc = _vmem->memstr(DEFAULT_BOOTARGS_STR_OTHER);
            default_boot_args_len = strlen(DEFAULT_BOOTARGS_STR_OTHER);
        }
    }

    retassure(default_boot_args_str_loc, "retassure: %d", __LINE__);
    default_boot_args_str_loc = dev ? default_boot_args_str_loc - 1 : default_boot_args_str_loc;
    debug("default_boot_args_str_loc=%p\n", default_boot_args_str_loc);

    if((_6723_100 || _7429_0) && !dev) {
        loc_t adr1 = 0;
        retassure(adr1 = find_literal_ref(default_boot_args_str_loc), "retassure: %d", __LINE__);
        debug("adr1=%p\n", adr1);
        vmem iter(*_vmem, adr1);
        while (++iter != insn::b) continue;
        loc_t bootargstackvarbranch = 0;
        retassure(bootargstackvarbranch = (loc_t)iter().imm(), "retassure: %d", __LINE__);
        debug("bootargstackvarbranch=%p\n", bootargstackvarbranch);
        iter = vmem(*_vmem,bootargstackvarbranch);
        while(++iter != insn::bl) continue;
        while(--iter != insn::nop) continue;
        loc_t bootargstackvar = iter().pc();
        retassure(default_boot_args_xref = bootargstackvar, "retassure: %d", __LINE__);
        debug("bootargstackvar=%p\n", bootargstackvar);
    } else {
        retassure(default_boot_args_xref = find_literal_ref(default_boot_args_str_loc), "retassure: %d", __LINE__);
        debug("default_boot_args_xref=%p\n",default_boot_args_xref);
    }

    debug("Relocating boot-args string...\n");
    loc_t cert_str_loc = 0;
    loc_t bootarg_loc1 = _vmem->memmem(_270ZEROES, 270, default_boot_args_xref);
    if(_chipid == 8010 || _chipid == 8003 || (_chipid == 8000 && !_7429_0)) {
        debug("Finding another bootarg location...\n");
        bootarg_loc1 = _vmem->memmem(_270ZEROES, 270, bootarg_loc1 + 270);
    }
    debug("bootarg_loc1=%p\n", bootarg_loc1);
    if(bootarg_loc1) {
        loc_t bootarg_loc = bootarg_loc1 + 0x11;
        debug("bootarg_loc=%p\n", bootarg_loc);
        vmem iter(*_vmem,bootarg_loc);
        while(true) {
            if(iter().opcode() == 0x00000000) {
                ++iter;
                if(iter().opcode() == 0x00000000) {
                    --iter;
                    break;
                }
                else
                    --iter;
            }
            ++iter;
        }

        debug("Pointing default boot-args xref to %p...\n", iter().pc() - 1);

        default_boot_args_str_loc = iter().pc() - 1;
    } else {
        /* Find the "Reliance on this cert..." string. */
        retassure(cert_str_loc = _vmem->memstr(CERT_STR), "Unable to find \"%s\" string!", CERT_STR);

        debug("\"%s\" string found at %p\n", CERT_STR, cert_str_loc);

        /* Point the boot-args xref to the "Reliance on this cert..." string. */
        debug("Pointing default boot-args xref to %p...\n", cert_str_loc);

        default_boot_args_str_loc = cert_str_loc;
    }

    vmem iter2(*_vmem, default_boot_args_xref);

    uint8_t _reg = 0;

    if ((_6723_100 || _7429_0) && !dev) {
      retassure(iter2() == insn::nop, "retassure: %d", __LINE__);
      loc_t adr2 = 0;
      retassure(adr2 = _vmem->memstr(DEFAULT_BOOTARGS_STR_OTHER2),
                "Unable to find \"%s\" string!\n",
                DEFAULT_BOOTARGS_STR_OTHER2);
      loc_t adr2_xref = 0;
      retassure(adr2_xref = find_literal_ref(adr2),
                "Unable to find \"%s\" xref for string!\n",
                DEFAULT_BOOTARGS_STR_OTHER2);
      iter2 = vmem(*_vmem, adr2_xref);
      if(_10151_0) {
        while (++iter2 != insn::sub)
          continue;
      } else {
        while (--iter2 != insn::sub)
          continue;
      }
      retassure(iter2() == insn::sub, "retassure: %d", __LINE__);
      retassure(iter2().rd(), "retassure: %d", __LINE__);
      _reg = iter2().rd();
    } else {
      if (iter2() != insn::adr) {
        --iter2;
        --iter2;
        retassure(iter2() == insn::bl, "retassure: %d", __LINE__);
        ++iter2;
        _reg = iter2().rd();
      } else {
        retassure(iter2() == insn::adr, "retassure: %d", __LINE__);
        _reg = iter2().rd();
      }
    }

    insn pins = insn::new_general_adr(
        default_boot_args_xref, (int64_t)default_boot_args_str_loc, _reg);

    uint32_t opcode = pins.opcode();
    patches.push_back({(loc_t)pins.pc(), &opcode, 4});

    debug("Applying custom boot-args \"%s\"\n", bootargs);
    patches.push_back(
        {default_boot_args_str_loc, bootargs, strlen(bootargs) + 1});

    vmem iter(*_vmem, default_boot_args_xref);
    uint8_t xrefRD = 0;
    if (_6723_100 || _7429_0) {
      xrefRD = 4;
    } else {
      xrefRD = iter().rd();
    }
    debug("xrefRD=%d\n", xrefRD);
    if (xrefRD > 9 || xrefRD == 4)
      return patches;

    while (++iter != insn::csel)
      ;

    insn csel = iter();
    debug("csel=%p\n", (loc_t)csel.pc());

    retassure(xrefRD == csel.rn() || xrefRD == csel.rm(), "retassure: %d", __LINE__);

    debug("cselrd=%d\n", csel.rd());

    {
      insn pins = insn::new_register_mov(iter, 0, csel.rd(), -1, xrefRD);
      debug("(%p)patching: \"mov x%d, x%d\"\n", (loc_t)pins.pc(), pins.rd(),
            pins.rm());
      uint32_t opcode = pins.opcode();
      patches.push_back({(loc_t)pins.pc(), &opcode, 4});
    }

    while ((--iter).supertype() != insn::sut_branch_imm || iter() == insn::bl)
      ;

    debug("branch loc=%p\n", (loc_t)iter);

    iter = (loc_t)iter().imm();

    debug("branch dst=%p\n", (loc_t)iter);

    if (iter() != insn::adr) {
      while (++iter != insn::adr)
        ;
    }

    {
      insn pins = insn::new_general_adr(
          iter, (int64_t)default_boot_args_str_loc, iter().rd());
      debug("(%p)patching: \"adr x%d, 0x%llx\"\n", (loc_t)pins.pc(),
            pins.rd(), pins.imm());
      uint32_t opcode = pins.opcode();
      patches.push_back({(loc_t)pins.pc(), &opcode, 4});
    }

    return patches;
}

std::vector<patch> ibootpatchfinder64_base::get_debug_enabled_patch(){
    std::vector<patch> patches;
    
    loc_t debug_enabled = findstr("debug-enabled", true);
    debug("debug_enabled=%p\n",debug_enabled);
    
    loc_t xref = find_literal_ref(debug_enabled);
    debug("xref=%p\n",xref);
    
    vmem iter(*_vmem,xref);
    
    while (++iter != insn::bl);
    while (++iter != insn::bl);
    
    patches.push_back({iter,"\x20\x00\x80\xD2" /* mov x0,1 */,4});
    
    return patches;
}

std::vector<patch> ibootpatchfinder64_base::get_cmd_handler_patch(const char *cmd_handler_str, uint64_t ptr){
    std::vector<patch> patches;
    std::string handler_str{"A"};
    handler_str+= cmd_handler_str;
    ((char*)handler_str.c_str())[0] = '\0';
    
    loc_t handler_str_loc = _vmem->memmem(handler_str.c_str(), handler_str.size());
    debug("handler_str_loc=%p\n",handler_str_loc);
    
    handler_str_loc++;
    
    loc_t tableref = _vmem->memmem(&handler_str_loc, sizeof(handler_str_loc));
    debug("tableref=%p\n",tableref);
    
    patches.push_back({tableref+8,&ptr,8});
    
    return patches;
}

std::vector<patch> ibootpatchfinder64_base::replace_bgcolor_with_memcpy(){
    std::vector<patch> patches;
    
    loc_t scratchbuf = _vmem->memstr("failed to execute upgrade command from new");
    debug("scratchbuf=%p\n",scratchbuf);

    std::string handler_str{"A"};
    handler_str+= "bgcolor";
    ((char*)handler_str.c_str())[0] = '\0';
    
    loc_t handler_str_loc = _vmem->memmem(handler_str.c_str(), handler_str.size());
    debug("handler_str_loc=%p\n",handler_str_loc);
    
    handler_str_loc++;
    
    loc_t tableref = _vmem->memmem(&handler_str_loc, sizeof(handler_str_loc));
    debug("tableref=%p\n",tableref);
    
    patches.push_back({scratchbuf,"memcpy",sizeof("memcpy")}); //overwrite name
    patches.push_back({tableref,&scratchbuf,8}); //overwrite pointer to name

    loc_t bgcolor = _vmem->deref(tableref+8);
    debug("bgcolor=%p\n",bgcolor);

    vmem iter(*_vmem,bgcolor);
    
    int seqLdr = 0;
    while (seqLdr != 3) {
        if ((++iter).supertype() == insn::sut_memory) {
            seqLdr++;
        }else{
            seqLdr = 0;
        }
    }
    do{
        //make ldrh to ldr
        insn pins = insn::new_immediate_ldr(iter, iter().imm(), iter().rn(), iter().rt());
        uint32_t opcode = pins.opcode();
        patches.push_back({iter, &opcode, 4});
        --iter;
    }while (--seqLdr > 0);
    
    while (++iter != insn::bl);
    
    loc_t overwritebl = iter;
    debug("overwritebl=%p\n",overwritebl);

    while (++iter != insn::ret);
    --iter;
    uint32_t backUpInsn = (uint32_t)_vmem->deref(iter);
    
    /*
     patch:
      ldrb       w3, [x1], #0x1
      strb       w3, [x0], #0x1
      subs       x2, x2, #0x1
      b.ne       cmd_bgcolor+84
     */
    
    constexpr const char patch[] = "\x23\x14\x40\x38\x03\x14\x00\x38\x42\x04\x00\xF1\xA1\xFF\xFF\x54";
    patches.push_back({overwritebl,patch,sizeof(patch)-1}); //my memcpy
    patches.push_back({overwritebl+sizeof(patch)-1,&backUpInsn,sizeof(backUpInsn)}); //epiloge
    patches.push_back({overwritebl+sizeof(patch)-1+sizeof(backUpInsn),"\xC0\x03\x5F\xD6",4}); //ret
    
    return patches;
}


std::vector<patch> ibootpatchfinder64_base::get_ra1nra1n_patch(){
    std::vector<patch> patches;
    
    /*
     uint32_t* tramp = find_next_insn(boot_image, 0x80000, 0xd2800012, 0xFFFFFFFF);
     if (tramp) {
         for (int i = 0; i < 5; i++) {
             tramp[i] = tramp_hook[i];
         }
     }
     
     patch ->
     
     
     mov x8, x27
     mov x9, x29
     mov x27, #0x800000000
     movk x27, #0x2800, lsl#16
     mov x29, x27
     
     */
    

    loc_t findloc = _vmem->memmem("\x12\x00\x80\xd2", 4);
    debug("findloc=%p\n",findloc);
    
    constexpr const char patch[] = "\xE8\x03\x1B\xAA\xE9\x03\x1D\xAA\x1B\x01\xC0\xD2\x1B\x00\xA5\xF2\xFD\x03\x1B\xAA";

    patches.push_back({findloc,patch,sizeof(patch)-1});

    loc_t findloc2 = _vmem->memmem("\x23\x74\x0b\xd5", 4);
    debug("findloc2=%p\n",findloc2);

    loc_t bzero = find_bof(findloc2);
    debug("bzero=%p\n",bzero);

    uint32_t nops[10];
    nops[0] = *(uint32_t*)"\x1F\x20\x03\xD5";
    for (int i=1; i<sizeof(nops)/sizeof(*nops); i++) {
        nops[i] = nops[0];
    }
    
    loc_t findNops = _vmem->memmem(nops, sizeof(nops));
    debug("findNops=%p\n",findNops);

    
    
    {
        insn pins = insn::new_immediate_b(bzero, (int64_t)findNops);
        uint32_t opcode = pins.opcode();
        patches.push_back({bzero, &opcode, 4});
    }
    
    constexpr const char patch2[] = "\x03\x01\xC0\xD2\x03\x00\xA5\xF2\x1F\x00\x03\xEB\xA8\x00\x00\x54\x22\x00\x00\x8B\x5F\x00\x03\xEB\x43\x00\x00\x54\xC0\x03\x1F\xD6";
    patches.push_back({findNops,patch2,sizeof(patch2)-1});
    
    loc_t aftershellcode = findNops +sizeof(patch2)-1;
    debug("aftershellcode=%p\n",aftershellcode);
    
    uint32_t backUpProloge = (uint32_t)_vmem->deref(bzero);
    
    patches.push_back({aftershellcode, &backUpProloge, 4});
    aftershellcode +=4;
    
    {
        insn pins = insn::new_immediate_b(aftershellcode, (int64_t)bzero+4);
        uint32_t opcode = pins.opcode();
        patches.push_back({aftershellcode, &opcode, 4});
    }
    
    
    
    return patches;
}


std::vector<patch> ibootpatchfinder64_base::get_unlock_nvram_patch(){
    std::vector<patch> patches;

    debug("check stage");
    if(stage1) {
        debug("iBootStage1 detected, not patching nvram");
        return patches;
    }
    debug("stage not iBootStage1, continuing patch");
    if(dev) {
        debug("DEVELOPMENT iBoot Detected, attempting simpler nvram patch");
        if(!stage2) {
            loc_t nvram_set_var_str = findstr("nvram_set_var", true);
            assure(nvram_set_var_str);
            debug("nvram_set_var_str=%p\n",nvram_set_var_str);
            loc_t nvram_set_var_str_xref = find_literal_ref(nvram_set_var_str);
            assure(nvram_set_var_str_xref);
            debug("nvram_set_var_str_xref=%p\n",nvram_set_var_str_xref);
            vmem iter(*_vmem,nvram_set_var_str_xref);
            while(--iter != insn::orr) continue;
            loc_t blacklist_func_top = iter().pc() - 4;
            debug("blacklist_func_top=%p\n",blacklist_func_top);
            patches.push_back({blacklist_func_top,"\x00\x00\x80\xD2"/* movz x0, #0x0*/"\xC0\x03\x5F\xD6"/*ret*/,4});
        } else {
            loc_t nvram_set_var_str = findstr("Blocked shadowed write to variable", false);
            assure(nvram_set_var_str);
            debug("nvram_set_var_str=%p\n",nvram_set_var_str);
            loc_t nvram_set_var_str_xref = find_literal_ref(nvram_set_var_str);
            assure(nvram_set_var_str_xref);
            debug("nvram_set_var_str_xref=%p\n",nvram_set_var_str_xref);
            vmem iter(*_vmem,nvram_set_var_str_xref);
            while(--iter != insn::nop) continue;
            loc_t blacklist_compare_nop = iter().pc();
            debug("blacklist_compare_nop=%p\n",blacklist_compare_nop);
            patches.push_back({blacklist_compare_nop,"\x33\x00\x80\x52"/* mov w19, #0x1*/,4});
        }
        loc_t com_apple_system = findstr("com.apple.System.", true);
        debug("com_apple_system=%p\n",com_apple_system);

        loc_t com_apple_system_xref = find_literal_ref(com_apple_system);
        debug("com_apple_system_xref=%p\n",com_apple_system_xref);

        loc_t func3top = find_bof(com_apple_system_xref);
        debug("func3top=%p\n",func3top);

        patches.push_back({func3top,"\x00\x00\x80\xD2"/* movz x0, #0x0*/"\xC0\x03\x5F\xD6"/*ret*/,8});
        return patches;
    }

    loc_t debug_uarts_str = findstr("debug-uarts", true);
    debug("debug_uarts_str=%p\n",debug_uarts_str);

    loc_t debug_uarts_ref = _vmem->memmem(&debug_uarts_str, sizeof(debug_uarts_str));
    if(dev) {
        debug_uarts_ref = _vmem->memmem(&debug_uarts_str, sizeof(debug_uarts_str), debug_uarts_ref + 4);
    }

    debug("debug_uarts_ref=%p\n",debug_uarts_ref);

    loc_t setenv_whitelist = debug_uarts_ref;
    
    if(_chipid == 7001 || _chipid == 8000 || _chipid == 8003) {
        debug("chipid == a8x/a9\n");
        setenv_whitelist-=16;
    } else {
        debug("chipid != a8x/a9\n");
        while (_vmem->deref(setenv_whitelist-=8));
        setenv_whitelist+=8;
    }
    debug("setenv_whitelist=%p\n",setenv_whitelist);

    loc_t blacklist1_func = find_literal_ref(setenv_whitelist);
    debug("blacklist1_func=%p\n",blacklist1_func);
    
    loc_t blacklist1_func_top = find_bof(blacklist1_func);
    debug("blacklist1_func_top=%p\n",blacklist1_func_top);

    patches.push_back({blacklist1_func_top, "\x00\x00\x80\xD2"/* movz x0, #0x0*/"\xC0\x03\x5F\xD6"/*ret*/, 8});

    loc_t env_whitelist = setenv_whitelist;
    while (_vmem->deref(env_whitelist+=8));
    env_whitelist+=8;
    debug("env_whitelist=%p\n",env_whitelist);

    loc_t blacklist2_func = find_literal_ref(env_whitelist);
    debug("blacklist2_func=%p\n",blacklist2_func);

    loc_t blacklist2_func_top = find_bof(blacklist2_func);
    debug("blacklist2_func_top=%p\n",blacklist2_func_top);
    
    patches.push_back({blacklist2_func_top,"\x00\x00\x80\xD2"/* movz x0, #0x0*/"\xC0\x03\x5F\xD6"/*ret*/,8});

    
    loc_t com_apple_system = findstr("com.apple.System.", true);
    debug("com_apple_system=%p\n",com_apple_system);

    loc_t com_apple_system_xref = find_literal_ref(com_apple_system);
    debug("com_apple_system_xref=%p\n",com_apple_system_xref);

    loc_t func3top = find_bof(com_apple_system_xref);
    debug("func3top=%p\n",func3top);

    patches.push_back({func3top,"\x00\x00\x80\xD2"/* movz x0, #0x0*/"\xC0\x03\x5F\xD6"/*ret*/,8});

    return patches;
}

std::vector<patch> ibootpatchfinder64_base::get_nvram_nosave_patch(){
    std::vector<patch> patches;

    loc_t saveenv_str = findstr("saveenv", true);
    debug("saveenv_str=%p\n",saveenv_str);

    loc_t saveenv_ref = _vmem->memmem(&saveenv_str, sizeof(saveenv_str));
    debug("saveenv_ref=%p\n",saveenv_ref);

    loc_t saveenv_cmd_func_pos = _vmem->deref(saveenv_ref+8);
    debug("saveenv_cmd_func_pos=%p\n",saveenv_cmd_func_pos);

    vmem saveenv_func(*_vmem,saveenv_cmd_func_pos);
    
    assure(saveenv_func() == insn::b);
    
    loc_t nvram_save_func = saveenv_func().imm();
    debug("nvram_save_func=%p\n",nvram_save_func);
    
    patches.push_back({nvram_save_func,"\xC0\x03\x5F\xD6"/*ret*/,4});
    return patches;
}

std::vector<patch> ibootpatchfinder64_base::get_nvram_noremove_patch(){
    std::vector<patch> patches;

    auto nosave_patches = get_nvram_nosave_patch();
    loc_t nvram_save_func = nosave_patches.at(0)._location;
    debug("nvram_save_func=%p\n",nvram_save_func);

    loc_t bootcommand_str = findstr("boot-command", true);
    debug("bootcommand_str=%p\n",bootcommand_str);
    
    loc_t remove_env_func = 0;
    
    for (int i=0;; i++) {
        loc_t bootcommand_ref = find_literal_ref(bootcommand_str,i);
        debug("[%d] bootcommand_ref=%p\n",i,bootcommand_ref);
        vmem iter(*_vmem,bootcommand_ref);
        
        for (int z=0; z<4; z++) {
            while (++iter != insn::bl);
            
            if (z == 0) { //this is the func where "boot-command" is passed as an argument
                remove_env_func = iter().imm();
                continue;
            }
            
            if (iter().imm() == nvram_save_func) { //after we unset an environment var, we usually do save_nvram within the next 3 functions
                goto found;
            }
        }
    }
    reterror("failed to find remove_env_func!"); //NOTREACHED
found:
    debug("remove_env_func=%p\n",remove_env_func);

    patches.push_back({remove_env_func,"\xC0\x03\x5F\xD6"/*ret*/,4});
    return patches;
}

std::vector<patch> ibootpatchfinder64_base::get_freshnonce_patch(){
    std::vector<patch> patches;

    debug("check stage");
    if(stage1) {
        debug("iBootStage1 detected, not patching nvram");
        return patches;
    }
    debug("stage not iBootStage1, continuing patch");

    loc_t noncevar_str = findstr("com.apple.System.boot-nonce", true);
    debug("noncevar_str=%p\n",noncevar_str);

    loc_t noncevar_ref = find_literal_ref(noncevar_str);
    debug("noncevar_ref=%p\n",noncevar_ref);

    loc_t noncefun1 = find_bof(noncevar_ref);
    debug("noncefun1=%p\n",noncefun1);

    loc_t noncefun1_blref = find_call_ref(noncefun1);
    debug("noncefun1_blref=%p\n",noncefun1_blref);

    loc_t noncefun2 = find_bof(noncefun1_blref);
    debug("noncefun2=%p\n",noncefun2);

    loc_t noncefun2_blref = find_call_ref(noncefun2);
    debug("noncefun2_blref=%p\n",noncefun2_blref);

    vmem iter(*_vmem,noncefun2_blref);
    
    while((--iter).supertype() != insn::sut_branch_imm) {
        continue;
    }

    loc_t branchloc = iter;
    debug("branchloc=%p\n",branchloc);

    patches.push_back({branchloc,"\x1F\x20\x03\xD5"/*nop*/,4});
    return patches;
}

//std::vector<patch> ibootpatchfinder64_base::get_readback_loadaddr_patch(){
//    std::vector<patch> patches;
//
//    loc_t cmd_results_str = findstr("cmd-results", true);
//    debug("cmd_results_str=%p\n",cmd_results_str);
//
//    loc_t cmd_results_ref = find_literal_ref(cmd_results_str);
//    debug("cmd_results_ref=%p\n",cmd_results_ref);
//
//    loc_t loadaddr_str = findstr("loadaddr", true);
//    debug("loadaddr_str=%p\n",loadaddr_str);
//
//    loc_t file_size_str = findstr("filesize", true);
//    debug("file_size_str=%p\n",file_size_str);
//    
//    
//    loc_t file_size_ref = find_literal_ref(file_size_str);
//    debug("file_size_ref=%p\n",file_size_ref);
//    
//    vmem iter(*_vmem,file_size_ref);
//
//    while (++iter != insn::bl);
//    
//    loc_t getenv_int_func = iter().imm();
//    debug("getenv_int_func=%p\n",getenv_int_func);
//
//
//    debug("Pointing cmd_results_ref to %p...\n", loadaddr_str);
//    {
//        insn pins = insn::new_general_adr(cmd_results_ref, (int64_t)loadaddr_str, 0);
//        uint32_t opcode = pins.opcode();
//        patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//
//    iter = cmd_results_ref;
//
//    while (++iter != insn::bl);
//
//    {
//        debug("replacing getenv func with getenvint func at=%p\n",(loc_t)iter);
//        insn pins = insn::new_immediate_bl(iter, (int64_t)getenv_int_func);
//        uint32_t opcode = pins.opcode();
//        patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//
//    ++iter;
//    ++iter;
//
//    debug("Loading file_size_str to x0\n");
//    loc_t loadArgLoc = iter;
//    debug("loadArgLoc=%p\n",loadArgLoc);
//    {
//        insn pins = insn::new_general_adr(loadArgLoc, (int64_t)file_size_str, 0);
//        uint32_t opcode = pins.opcode();
//        patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//
//    debug("Calling getenv\n");
//    ++iter;
//    loc_t callGentenvLoc = iter;
//    debug("callGentenvLoc=%p\n",callGentenvLoc);
//    {
//        insn pins = insn::new_immediate_bl(callGentenvLoc, (int64_t)getenv_int_func);
//        uint32_t opcode = pins.opcode();
//        patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//    
//    while (++iter != insn::bl);
//    
//    loc_t strlenloc = iter;
//    debug("strlenloc=%p\n",strlenloc);
//    patches.push_back({strlenloc,"\xE1\x03\x00\xAA"/*mov x1, x0*/"\x1F\x20\x03\xD5"/*nop*/,8});
//    return patches;
//}


//std::vector<patch> ibootpatchfinder64_base::get_memload_patch(){
//    std::vector<patch> patches;
//
//    loc_t loadaddr_str = findstr("loadaddr", true);
//    debug("loadaddr_str=%p\n",loadaddr_str);
//
//    loc_t memboot_str = findstr("memboot", true);
//    debug("memboot_str=%p\n",memboot_str);
//
//    debug("renaming memboot to memload\n");
//    patches.push_back({memboot_str,"memload",strlen("memload")});
//
//    loc_t memboot_table_ptr = _vmem->memmem(&memboot_str, sizeof(memboot_str));
//    debug("memboot_table_ptr=%p\n",memboot_table_ptr);
//
//    memboot_table_ptr+=8;
//
//    loc_t memboot_fuc = _vmem->deref(memboot_table_ptr);
//    debug("memboot_fuc=%p\n",memboot_fuc);
//
//    vmem iter(*_vmem,memboot_fuc);
//
//    while (++iter != insn::bl);
//
//    loc_t firstBL = iter;
//    debug("firstBL=%p\n",firstBL);
//
//    loc_t getenv_func = iter().imm();
//    debug("getenv_func=%p\n",getenv_func);
//
//    while (++iter != insn::cbz);
//
//    loc_t fistCBZ = iter;
//    debug("fistCBZ=%p\n",fistCBZ);
//
//    loc_t cbzdst = iter().imm();
//    debug("cbzdst=%p\n",cbzdst);
//
//
//    loc_t err_loading_ramdisk_str = findstr("error loading ramdisk\n", true);
//    debug("err_loading_ramdisk_str=%p\n",err_loading_ramdisk_str);
//
//    loc_t err_loading_ramdisk_ref = find_literal_ref(err_loading_ramdisk_str);
//    debug("err_loading_ramdisk_ref=%p\n",err_loading_ramdisk_ref);
//
//
//    loc_t bsrc = find_branch_ref(err_loading_ramdisk_ref, -0x200);
//    debug("bsrc=%p\n",bsrc);
//
//    iter = bsrc;
//
//    while (--iter != insn::bl);
//
//    loc_t load_ramdisk_func = iter().imm();
//    debug("load_ramdisk_func=%p\n",load_ramdisk_func);
//
//    iter = load_ramdisk_func;
//    do{
//        while (++iter != insn::bl);
//    }while (find_register_value(iter,3) != 'rdsk');
//
//    loc_t loadimg_func = iter().imm();
//    debug("loadimg_func=%p\n",loadimg_func);
//
//
//    iter = firstBL;
//    ++iter;
//
//    uint8_t backupreg = iter().rd();
//    debug("filesize reg=%u\n",backupreg);
//    ++iter;
//
//    {
//        debug("arg0 = loadaddr\n");
//        insn pins = insn::new_general_adr(iter, (int64_t)loadaddr_str, 0);
//        uint32_t opcode = pins.opcode();
//        patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//    ++iter;
//    {
//        debug("call getenv(loadaddr)\n");
//        insn pins = insn::new_immediate_bl(iter, (int64_t)getenv_func);
//        uint32_t opcode = pins.opcode();
//        patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//    ++iter;
//    {
//        debug("x1 = filesize_val\n");
//        insn pins = insn::new_register_mov(iter, 0, 1, -1, backupreg);
//        uint32_t opcode = pins.opcode();
//        patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//    ++iter;
//    debug("x2 = 'ibot'\n");
//    {
//       debug("  x2 = '  ot'\n");
//       insn pins = insn::new_immediate_movz(iter, 'ibot' & 0xffff, 2, 0);
//       uint32_t opcode = pins.opcode();
//       patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//    ++iter;
//    {
//       debug("  x2 |= 'ib  '\n");
//       insn pins = insn::new_immediate_movk(iter, ('ibot'>>16) & 0xffff, 2, 1);
//       uint32_t opcode = pins.opcode();
//       patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//    ++iter;
//    {
//       debug("x3 = 0\n");
//       insn pins = insn::new_immediate_movz(iter, 0, 3, 0);
//       uint32_t opcode = pins.opcode();
//       patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
//    ++iter;
//    {
//       debug("call load image\n");
//       insn pins = insn::new_immediate_bl(iter, (int64_t)loadimg_func);
//       uint32_t opcode = pins.opcode();
//       patches.push_back({(loc_t)pins.pc(), &opcode, 4});
//    }
////    ++iter;
////    {
////       debug("jump exit\n");
////       insn pins(iter, insn::b, insn::st_immediate, cbzdst, 0, 0, 0, 0);
////       uint32_t opcode = pins.opcode();
////       patches.push_back({(loc_t)pins.pc(), &opcode, 4});
////    }
//    return patches;
//}
