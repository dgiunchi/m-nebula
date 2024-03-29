#define N_IMPLEMENTS nDefaultLogHandler
#define N_KERNEL
//------------------------------------------------------------------------------
//  ndefaultloghandler.cc
//  (C) 2003 RadonLabs GmbH
//------------------------------------------------------------------------------
#include "kernel/ndefaultloghandler.h"
#include <string.h>

//------------------------------------------------------------------------------
/**
*/
nDefaultLogHandler::nDefaultLogHandler()
{
    // empty
}

//------------------------------------------------------------------------------
/**
*/
nDefaultLogHandler::~nDefaultLogHandler()
{
    // empty
}

//------------------------------------------------------------------------------
/**
*/
void
nDefaultLogHandler::Print(const char* msg, va_list argList)
{
    vprintf(msg, argList);
    this->PutLineBuffer(msg, argList);
}

//------------------------------------------------------------------------------
/**
*/
void
nDefaultLogHandler::Message(const char* msg, va_list argList)
{
    vprintf(msg, argList);
    this->PutLineBuffer(msg, argList);
}

//------------------------------------------------------------------------------
/**
*/
void
nDefaultLogHandler::Error(const char* msg, va_list argList)
{
    vprintf(msg, argList);
    this->PutLineBuffer(msg, argList);
}

//------------------------------------------------------------------------------
/**
    Return a pointer to the internal line buffer.
*/
nLineBuffer*
nDefaultLogHandler::GetLineBuffer()
{
    return &(this->lineBuffer);
}

//------------------------------------------------------------------------------
/**
    Put a message into the argbuffer.

    @param  prefix  a short message which will be put in front of the actual message
    @param  msg     the actual message
    @param  argList a var args list
*/
void
nDefaultLogHandler::PutLineBuffer(const char* msg, va_list argList)
{
    // FIXME: argh, this is not save, and also slow!
    n_assert(msg);
    //n_assert(strlen(msg) < 1024);
    static char charBuffer[N_MAXSTR*16];
    vsprintf_s(charBuffer, N_MAXSTR*16, msg, argList);
    this->lineBuffer.Put(charBuffer);
}


