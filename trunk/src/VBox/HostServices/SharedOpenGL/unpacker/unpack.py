# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

from __future__ import print_function
import sys

import apiutil


apiutil.CopyrightC()

print("""/* DO NOT EDIT!  THIS CODE IS AUTOGENERATED BY unpack.py */

#include "unpacker.h"
#include "cr_opcodes.h"
#include "cr_error.h"
#include "cr_mem.h"
#include "cr_spu.h"
#include "unpack_extend.h"
#include <stdio.h>
#include <memory.h>

#include <iprt/cdefs.h>

DECLEXPORT(const unsigned char *) cr_unpackData = NULL;
DECLEXPORT(const unsigned char *) cr_unpackDataEnd = NULL;

static void crUnpackExtend(PCrUnpackerState pState);
static void crUnpackExtendDbg(PCrUnpackerState pState);

#if 0 //def DEBUG_misha
//# define CR_UNPACK_DEBUG_OPCODES
# define CR_UNPACK_DEBUG_LAST_OPCODES
# define CR_UNPACK_DEBUG_PREV_OPCODES
#endif

#ifdef CR_UNPACK_DEBUG_PREV_OPCODES
static GLenum g_VBoxDbgCrPrevOpcode = 0;
static GLenum g_VBoxDbgCrPrevExtendOpcode = 0;
#endif
""")

nodebug_opcodes = [
    "CR_MULTITEXCOORD2FARB_OPCODE",
    "CR_VERTEX3F_OPCODE",
    "CR_NORMAL3F_OPCODE",
    "CR_COLOR4UB_OPCODE",
    "CR_LOADIDENTITY_OPCODE",
    "CR_MATRIXMODE_OPCODE",
    "CR_LOADMATRIXF_OPCODE",
    "CR_DISABLE_OPCODE",
    "CR_COLOR4F_OPCODE",
    "CR_ENABLE_OPCODE",
    "CR_BEGIN_OPCODE",
    "CR_END_OPCODE",
    "CR_SECONDARYCOLOR3FEXT_OPCODE"
]

nodebug_extopcodes = [
    "CR_ACTIVETEXTUREARB_EXTEND_OPCODE"
]

#
# Useful functions
#

def ReadData( offset, arg_type ):
    """Emit a READ_DOUBLE or READ_DATA call for pulling a GL function
    argument out of the buffer's operand area."""
    if arg_type == "GLdouble" or arg_type == "GLclampd":
        retval = "READ_DOUBLE(pState, %d)" % offset
    else:
        retval = "READ_DATA(pState, %d, %s)" % (offset, arg_type)
    return retval


def FindReturnPointer( return_type, params ):
    """For GL functions that return values (either as the return value or
    through a pointer parameter) emit a SET_RETURN_PTR call."""
    arg_len = apiutil.PacketLength( params )
    if (return_type != 'void'):
        print('\tSET_RETURN_PTR(pState, %d);' % (arg_len + 8)) # extended opcode plus packet length
    else:
        paramList = [ ('foo', 'void *', 0) ]
        print('\tSET_RETURN_PTR(pState, %d);' % (arg_len + 8 - apiutil.PacketLength(paramList)))


def getWritebackPointerOffset(return_type, params):
    """Returns the offset of the writeback pointer"""
    arg_len = apiutil.PacketLength( params )
    if return_type != 'void':
        paramList = [ ('foo', 'void *', 0) ]
        arg_len += apiutil.PacketLength( paramList )

    return arg_len + 8 # extended opcode plus packet length

def FindWritebackPointer( return_type, params ):
    """Emit a SET_WRITEBACK_PTR call."""
    print('\tSET_WRITEBACK_PTR(pState, %d);' % getWritebackPointerOffset(return_type, params))


def MakeNormalCall( return_type, func_name, params, counter_init = 0 ):
    counter = counter_init
    copy_of_params = params[:]

    for i in range( 0, len(params) ):
        (name, type, vecSize) = params[i]
        if apiutil.IsPointer(copy_of_params[i][1]):
            params[i] = ('NULL', type, vecSize)
            copy_of_params[i] = (copy_of_params[i][0], 'void', 0)
            if not "get" in apiutil.Properties(func_name):
                print('\tcrError( "%s needs to be special cased!" );' % func_name)
        else:
            print("\t%s %s = %s;" % ( copy_of_params[i][1], name, ReadData( counter, copy_of_params[i][1] ) ))
        counter += apiutil.sizeof(copy_of_params[i][1])

    if ("get" in apiutil.Properties(func_name)):
        FindReturnPointer( return_type, params )
        FindWritebackPointer( return_type, params )

    if return_type != "void":
        print("\t(void)", end=" ")
    else:
        print("\t", end="")
    print("pState->pDispatchTbl->%s(%s);" % (func_name, apiutil.MakeCallStringForDispatcher(params)))


def MakeVectorCall( return_type, func_name, arg_type ):
    """Convert a call like glVertex3f to glVertex3fv."""
    vec_func = apiutil.VectorFunction(func_name)
    params = apiutil.Parameters(vec_func)
    assert len(params) == 1
    (arg_name, vecType, vecSize) = params[0]

    if arg_type == "GLdouble" or arg_type == "GLclampd":
        print("#ifdef CR_UNALIGNED_ACCESS_OKAY")
        print("\tpState->pDispatchTbl->%s((%s) pState->pbUnpackData);" % (vec_func, vecType))
        print("#else")
        for index in range(0, vecSize):
            print("\tGLdouble v" + repr(index) + " = READ_DOUBLE(pState, " + repr(index * 8) + ");")
        if return_type != "void":
            print("\t(void) pState->pDispatchTbl->%s(" % func_name, end="")
        else:
            print("\tpState->pDispatchTbl->%s(" % func_name, end="")
        for index in range(0, vecSize):
            print("v" + repr(index), end="")
            if index != vecSize - 1:
                print(",", end=" ")
        print(");")
        print("#endif")
    else:
        print("\tpState->pDispatchTbl->%s((%s) pState->pbUnpackData);" % (vec_func, vecType))



keys = apiutil.GetDispatchedFunctions(sys.argv[1]+"/APIspec.txt")


#
# Generate unpack functions for all the simple functions.
#
for func_name in keys:
    if (not "pack" in apiutil.ChromiumProps(func_name) or
        apiutil.FindSpecial( "unpacker", func_name )):
        continue

    params = apiutil.Parameters(func_name)
    return_type = apiutil.ReturnType(func_name)

    packet_length = apiutil.PacketLength( params )
    print("static void crUnpack%s(PCrUnpackerState pState)" % func_name)
    print("{")
    if ("get" in apiutil.Properties(func_name)):
        print("\tCHECK_BUFFER_SIZE_STATIC(pState, %s);" % getWritebackPointerOffset(return_type, params));
    elif packet_length != 0:
        print("\tCHECK_BUFFER_SIZE_STATIC(pState, %s);" % packet_length);

    vector_func = apiutil.VectorFunction(func_name)
    if (vector_func and len(apiutil.Parameters(vector_func)) == 1):
        MakeVectorCall( return_type, func_name, params[0][1] )
    else:
        MakeNormalCall( return_type, func_name, params )
    if packet_length == 0:
        print("\tINCR_DATA_PTR_NO_ARGS(pState);")
    else:
        print("\tINCR_DATA_PTR(pState, %d);" % packet_length)
    print("}\n")


#
# Emit some code
#
print(""" 
CR_UNPACK_BUFFER_TYPE crUnpackGetBufferType(const void *opcodes, unsigned int num_opcodes)
{
    const uint8_t *pu8Codes = (const uint8_t *)opcodes;

    uint8_t first;
    uint8_t last;

    if (!num_opcodes)
        return CR_UNPACK_BUFFER_TYPE_GENERIC;

    first = pu8Codes[0];
    last = pu8Codes[1-(int)num_opcodes];
    
    switch (last)
    {
        case CR_CMDBLOCKFLUSH_OPCODE:
            return CR_UNPACK_BUFFER_TYPE_CMDBLOCK_FLUSH;
        case CR_CMDBLOCKEND_OPCODE:
            return (first == CR_CMDBLOCKBEGIN_OPCODE) ? CR_UNPACK_BUFFER_TYPE_GENERIC : CR_UNPACK_BUFFER_TYPE_CMDBLOCK_END;
        default:
            return (first != CR_CMDBLOCKBEGIN_OPCODE) ? CR_UNPACK_BUFFER_TYPE_GENERIC : CR_UNPACK_BUFFER_TYPE_CMDBLOCK_BEGIN;
    } 
}

void crUnpack(PCrUnpackerState pState)
{
    unsigned int i;

#if defined(CR_UNPACK_DEBUG_OPCODES) || defined(CR_UNPACK_DEBUG_LAST_OPCODES)
    crDebug("crUnpack: %d opcodes", pState->cOpcodes);
#endif

    for (i = 0; i < pState->cOpcodes && RT_SUCCESS(pState->rcUnpack); i++)
    {
    
        CRDBGPTR_CHECKZ(pState->pWritebackPtr);
        CRDBGPTR_CHECKZ(pState->pReturnPtr);
    
        /*crDebug(\"Unpacking opcode \%d\", *pState->pbOpcodes);*/
#ifdef CR_UNPACK_DEBUG_PREV_OPCODES
        g_VBoxDbgCrPrevOpcode = *pState->pbOpcodes;
#endif
        switch( *pState->pbOpcodes )
        {""")

#
# Emit switch cases for all unextended opcodes
#
for func_name in keys:
    if "pack" in apiutil.ChromiumProps(func_name):
        print('\t\t\tcase %s:' % apiutil.OpcodeName( func_name ))
        if not apiutil.OpcodeName(func_name) in nodebug_opcodes:
            print("""
#ifdef CR_UNPACK_DEBUG_LAST_OPCODES
                if (i==(num_opcodes-1))
#endif
#if defined(CR_UNPACK_DEBUG_OPCODES) || defined(CR_UNPACK_DEBUG_LAST_OPCODES)
                crDebug("Unpack: %s");
#endif """ % apiutil.OpcodeName(func_name))
        print('\t\t\t\tcrUnpack%s(pState); \n\t\t\t\tbreak;' % func_name)

print("""       
            case CR_EXTEND_OPCODE:
                #ifdef CR_UNPACK_DEBUG_OPCODES 
                    crUnpackExtendDbg(pState);
                #else
                # ifdef CR_UNPACK_DEBUG_LAST_OPCODES
                    if (i==(num_opcodes-1)) crUnpackExtendDbg(pState);
                    else
                # endif
                    crUnpackExtend(pState);
                #endif
                break;
            case CR_CMDBLOCKBEGIN_OPCODE:
            case CR_CMDBLOCKEND_OPCODE:
            case CR_CMDBLOCKFLUSH_OPCODE:
            case CR_NOP_OPCODE:
                INCR_DATA_PTR_NO_ARGS(pState);
                break;
            default:
                crError( "Unknown opcode: %d", *pState->pbOpcodes );
                break;
        }

        CRDBGPTR_CHECKZ(pState->pWritebackPtr);
        CRDBGPTR_CHECKZ(pState->pReturnPtr);
        pState->pbOpcodes--;
    }
}""")


#
# Emit unpack functions for extended opcodes, non-special functions only.
#
for func_name in keys:
        if ("extpack" in apiutil.ChromiumProps(func_name)
            and not apiutil.FindSpecial("unpacker", func_name)):
            return_type = apiutil.ReturnType(func_name)
            params = apiutil.Parameters(func_name)
            print('static void crUnpackExtend%s(PCrUnpackerState pState)' % func_name)
            print('{')
            if ("get" in apiutil.Properties(func_name)):
                print("\tCHECK_BUFFER_SIZE_STATIC(pState, %s);" % (8 + getWritebackPointerOffset(return_type, params)));
            else:
                print("\tCHECK_BUFFER_SIZE_STATIC(pState, %s);" % (8 + apiutil.PacketLength( params )));
            MakeNormalCall( return_type, func_name, params, 8 )
            print('}\n')

print('static void crUnpackExtend(PCrUnpackerState pState)')
print('{')
print('\tCHECK_BUFFER_SIZE_STATIC_LAST(pState, 4, GLenum);');
print('\tGLenum extend_opcode = %s;' % ReadData( 4, 'GLenum' ))
print('')
print('#ifdef CR_UNPACK_DEBUG_PREV_OPCODES')
print('\tg_VBoxDbgCrPrevExtendOpcode = extend_opcode;')
print('#endif')
print('')
print('\t/*crDebug(\"Unpacking extended opcode \%d", extend_opcode);*/')
print('\tswitch( extend_opcode )')
print('\t{')


#
# Emit switch statement for extended opcodes
#
for func_name in keys:
    if "extpack" in apiutil.ChromiumProps(func_name):
        print('\t\tcase %s:' % apiutil.ExtendedOpcodeName( func_name ))
#        print('\t\t\t\tcrDebug("Unpack: %s");' % apiutil.ExtendedOpcodeName( func_name )))
        print('\t\t\tcrUnpackExtend%s(pState);' % func_name)
        print('\t\t\tbreak;')

print("""       default:
            crError( "Unknown extended opcode: %d", (int) extend_opcode );
            break;
    }
    INCR_VAR_PTR(pState);
}""")

print('static void crUnpackExtendDbg(PCrUnpackerState pState)')
print('{')
print('\tCHECK_BUFFER_SIZE_STATIC_LAST(pState, 4, GLenum);');
print('\tGLenum extend_opcode = %s;' % ReadData( 4, 'GLenum' ))
print('')
print('#ifdef CR_UNPACK_DEBUG_PREV_OPCODES')
print('\tg_VBoxDbgCrPrevExtendOpcode = extend_opcode;')
print('#endif')
print('')
print('\t/*crDebug(\"Unpacking extended opcode \%d", extend_opcode);*/')
print('\tswitch( extend_opcode )')
print('\t{')


#
# Emit switch statement for extended opcodes
#
for func_name in keys:
    if "extpack" in apiutil.ChromiumProps(func_name):
        print('\t\tcase %s:' % apiutil.ExtendedOpcodeName( func_name ))
        if not apiutil.ExtendedOpcodeName(func_name) in nodebug_extopcodes:
            print('\t\t\tcrDebug("Unpack: %s");' % apiutil.ExtendedOpcodeName( func_name ))
        print('\t\t\tcrUnpackExtend%s(pState);' % func_name)
        print('\t\t\tbreak;')

print("""       default:
            crError( "Unknown extended opcode: %d", (int) extend_opcode );
            break;
    }
    INCR_VAR_PTR(pState);
}""")
