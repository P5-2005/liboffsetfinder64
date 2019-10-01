//
//  ibootpatchfinder64.cpp
//  liboffsetfinder64
//
//  Created by tihmstar on 28.09.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "ibootpatchfinder64.hpp"
#include "all_liboffsetfinder.hpp"
#include <liboffsetfinder64/libgeneral/macros.h>
#include "OFexception.hpp"

using namespace std;
using namespace tihmstar::offsetfinder64;

#define IBOOT_VERS_STR_OFFSET 0x280
#define iBOOT_BASE_OFFSET 0x318
#define KERNELCACHE_PREP_STRING "__PAGEZERO"
#define ENTERING_RECOVERY_CONSOLE "Entering recovery mode, starting command prompt"
#define DEBUG_ENABLED_DTRE_VAR_STR "debug-enabled"
#define DEFAULT_BOOTARGS_STR "rd=md0 nand-enable-reformat=1 -progress"
#define CERT_STR "Apple Secure Boot Root CA"

ibootpatchfinder64::ibootpatchfinder64(const char * filename) :
    patchfinder64(true),
    _vers(0)
{
    struct stat fs = {0};
    int fd = 0;
    bool didConstructSuccessfully = false;
    cleanup([&]{
        if (fd>0) close(fd);
        if (!didConstructSuccessfully) {
            safeFree(_buf);
        }
    })
    
    assure((fd = open(filename, O_RDONLY)) != -1);
    assure(!fstat(fd, &fs));
    assure((_buf = (uint8_t*)malloc( _bufSize = fs.st_size)));
    assure(read(fd,_buf,_bufSize)==_bufSize);
    
    assure(_bufSize > 0x1000);
    
    assure(!strncmp((char*)&_buf[IBOOT_VERS_STR_OFFSET], "iBoot", sizeof("iBoot")-1));
    retassure(*(uint32_t*)&_buf[0] == 0x90000000, "invalid magic");
    
    _entrypoint = _base = (loc_t)*(uint64_t*)&_buf[iBOOT_BASE_OFFSET];
    
    _vmem = new vmem({{_buf,_bufSize,_base, vsegment::vmprot::kVMPROTREAD | vsegment::vmprot::kVMPROTWRITE | vsegment::vmprot::kVMPROTEXEC}});
    
    retassure(_vers = atoi((char*)&_buf[IBOOT_VERS_STR_OFFSET+6]), "No iBoot version found!\n");
    
    debug("iBoot-%d inputted\n", _vers);
    
    didConstructSuccessfully = true;
}

bool ibootpatchfinder64::has_kernel_load(){
    return (bool) (_vmem->memstr(KERNELCACHE_PREP_STRING) != NULL);
}

bool ibootpatchfinder64::has_recovery_console(){
    return (bool) (_vmem->memstr(ENTERING_RECOVERY_CONSOLE) != NULL);
}


std::vector<patch> ibootpatchfinder64::get_boot_arg_patch(const char *bootargs){
    std::vector<patch> patches;
    loc_t default_boot_args_str_loc = NULL;
    loc_t default_boot_args_xref = NULL;

    assure(default_boot_args_str_loc = _vmem->memstr(DEFAULT_BOOTARGS_STR));
    debug("default_boot_args_str_loc=%p\n",default_boot_args_str_loc);
   
    assure(default_boot_args_xref = find_literal_ref(default_boot_args_str_loc));
    debug("default_boot_args_xref=%p\n",default_boot_args_xref);

    if (strlen(bootargs) > strlen(DEFAULT_BOOTARGS_STR)) {
        loc_t cert_str_loc = NULL;
        debug("Relocating boot-args string...\n");

        /* Find the "Reliance on this cert..." string. */
        retassure(cert_str_loc = _vmem->memstr(CERT_STR), "Unable to find \"%s\" string!\n", CERT_STR);

        debug("\"%s\" string found at %p\n", CERT_STR, cert_str_loc);

        /* Point the boot-args xref to the "Reliance on this cert..." string. */
        debug("Pointing default boot-args xref to %p...\n", cert_str_loc);

        default_boot_args_str_loc = cert_str_loc;

        insn pins(default_boot_args_xref, insn::adr, insn::st_general, (int64_t)default_boot_args_str_loc, 9, 0, 0, 0);
        uint32_t opcode = pins.opcode();
        patches.push_back({(loc_t)pins.pc(), &opcode, 4});
    }
    
    debug("Applying custom boot-args \"%s\"\n", bootargs);
    patches.push_back({default_boot_args_str_loc, bootargs, strlen(bootargs)+1});
    
    vmem iter(*_vmem,default_boot_args_xref);
    uint8_t xrefRD = iter().rd();
    debug("xrefRD=%d\n",xrefRD);

    
    while (++iter != insn::csel);
    
    insn csel = iter();
    debug("csel=%p\n", (loc_t)csel.pc());

    assure(xrefRD == csel.rn() || xrefRD == csel.other());
    
    debug("cselrd=%d\n",csel.rd());
    
    insn pmov(iter(), insn::movz, insn::st_register, 0, csel.rd(), -1, 0, xrefRD);
    
    debug("(%p)patching: \"mov x%d, x%d\"\n",(loc_t)pmov.pc(),pmov.rd(),pmov.other());
    uint32_t opcode = pmov.opcode();
    patches.push_back({(loc_t)pmov.pc(), &opcode, 4});

    
    while ((--iter).supertype() != insn::sut_branch_imm || iter() == insn::bl);
    
    debug("branch loc=%p\n",(loc_t)iter);
    
    iter = (loc_t)iter().imm();

    debug("branch dst=%p\n",(loc_t)iter);
    
    if (iter() != insn::adr) {
        while (++iter != insn::adr);
    }

    insn pins(iter, insn::adr, insn::st_general, (int64_t)default_boot_args_str_loc, iter().rd(), 0, 0, 0);
    opcode = pins.opcode();
    patches.push_back({(loc_t)pins.pc(), &opcode, 4});
    debug("(%p)patching: \"adr x%d, 0x%llx\"\n",(loc_t)pins.pc(),pins.rd(),pins.imm());

    return patches;
}

std::vector<patch> ibootpatchfinder64::get_debug_enabled_patch(){
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

std::vector<patch> ibootpatchfinder64::get_cmd_handler_patch(const char *cmd_handler_str, uint64_t ptr){
    std::vector<patch> patches;
    std::string handler_str{"A"};
    handler_str+= cmd_handler_str;
    ((char*)handler_str.c_str())[0] = '\0';
    
    loc_t handler_str_loc = _vmem->memmem(handler_str.c_str(), handler_str.size());
    debug("handler_str_loc=%p\n",handler_str_loc);

    handler_str_loc++;

    loc_t tableref = _vmem->memmem(&handler_str_loc, sizeof(handler_str_loc));
    debug("tableref=%p\n",tableref);

    patches.push_back({tableref+8,&ptr,sizeof(ptr)});

    return patches;
}


std::vector<patch> ibootpatchfinder64::get_sigcheck_patch(){
    std::vector<patch> patches;
    loc_t img4str = findstr("IMG4", true);
    debug("img4str=%p\n",img4str);

    loc_t img4strref = find_literal_ref(img4str);
    debug("img4strref=%p\n",img4strref);

    loc_t f1top = find_bof(img4strref);
    debug("f1top=%p\n",f1top);

    loc_t f1topref = find_branch_ref(f1top,1);
    debug("f1topref=%p\n",f1topref);

    loc_t f2top = find_bof(f1topref);
    debug("f2top=%p\n",f2top);

    vmem iter(*_vmem,f2top);

    loc_t adr_x3 = 0;
    loc_t adr_x2 = 0;

    while (true) {
        if (++iter == insn::adr && iter().rd() == 2){
            adr_x2 = iter;
        }else if (iter() == insn::adr && iter().rd() == 3){
            adr_x3 = iter;
        }else if (iter() == insn::bl){
            if (adr_x2 && adr_x3) {
                break;
            }else{
                adr_x2 = 0;
                adr_x3 = 0;
            }
        }
    }
    
    assure(adr_x2);
    iter = adr_x2;
    
    loc_t callback = (loc_t)_vmem->deref((loc_t)iter().imm());
    debug("callback=%p\n",callback);

    iter = callback;
    
    while (++iter != insn::ret);
    
    loc_t ret = iter;
    debug("ret=%p\n",ret);

    const char p[] ="\x00\x00\x80\xD2" /*mov x0,0*/ "\xC0\x03\x5F\xD6" /*ret*/;
    patches.push_back({ret,p,sizeof(p)-1});
    
    return patches;
}