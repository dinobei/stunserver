/*
   Copyright 2011 John Selbie

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/


#include "commonincludes.h"
#include <openssl/hmac.h>
#include "stuncore.h"
#include "stunsocket.h"
#include "stunsocketthread.h"
#include "server.h"



CStunServerConfig::CStunServerConfig() :
fHasPP(false),
fHasPA(false),
fHasAP(false),
fHasAA(false),
fMultiThreadedMode(false)
{
    ;
}



CStunServer::CStunServer() :
_arrSockets() // zero-init
{
    ;
}

CStunServer::~CStunServer()
{
    Shutdown();
}

HRESULT CStunServer::Initialize(const CStunServerConfig& config)
{
    HRESULT hr = S_OK;
    int socketcount = 0;
    CRefCountedPtr<IStunAuth> _spAuth;

    // cleanup any thing that's going on now
    Shutdown();
    
    // optional code: create an authentication provider and initialize it here (if you want authentication)
    // set the _spAuth member to reference it
    // Chk(CYourAuthProvider::CreateInstanceNoInit(&_spAuth));

    // Create the sockets
    if (config.fHasPP)
    {
        Chk(CStunSocket::CreateUDP(config.addrPP, RolePP, &_arrSockets[RolePP]));
        _arrSockets[RolePP]->EnablePktInfoOption(true);
        socketcount++;
    }

    if (config.fHasPA)
    {
        Chk(CStunSocket::CreateUDP(config.addrPA, RolePA, &_arrSockets[RolePA]));
        _arrSockets[RolePA]->EnablePktInfoOption(true);
        socketcount++;
    }

    if (config.fHasAP)
    {
        Chk(CStunSocket::CreateUDP(config.addrAP, RoleAP, &_arrSockets[RoleAP]));
        _arrSockets[RoleAP]->EnablePktInfoOption(true);
        socketcount++;
    }

    if (config.fHasAA)
    {
        Chk(CStunSocket::CreateUDP(config.addrAA, RoleAA, &_arrSockets[RoleAA]));
        _arrSockets[RoleAA]->EnablePktInfoOption(true);
        socketcount++;
    }

    ChkIf(socketcount == 0, E_INVALIDARG);


    if (config.fMultiThreadedMode == false)
    {
        Logging::LogMsg(LL_DEBUG, "Configuring single threaded mode\n");

        // create one thread for all the sockets
        CStunSocketThread* pThread = new CStunSocketThread();
        ChkIf(pThread==NULL, E_OUTOFMEMORY);

        _threads.push_back(pThread);
        
        Chk(pThread->Init(_arrSockets,  _spAuth, (SocketRole)-1));
    }
    else
    {
        Logging::LogMsg(LL_DEBUG, "Configuring multi-threaded mode\n");

        // one thread for every socket
        CStunSocketThread* pThread = NULL;
        for (size_t index = 0; index < ARRAYSIZE(_arrSockets); index++)
        {
            if (_arrSockets[index] != NULL)
            {
                SocketRole rolePrimaryRecv = _arrSockets[index]->GetRole();
                ASSERT(rolePrimaryRecv == (SocketRole)index);
                pThread = new CStunSocketThread();
                ChkIf(pThread==NULL, E_OUTOFMEMORY);
                _threads.push_back(pThread);
                Chk(pThread->Init(_arrSockets, _spAuth, rolePrimaryRecv));
            }
        }
    }


Cleanup:

    if (FAILED(hr))
    {
        Shutdown();
    }

    return hr;

}

HRESULT CStunServer::Shutdown()
{
    size_t len;

    Stop();

    // release the sockets and the thread

    for (size_t index = 0; index < ARRAYSIZE(_arrSockets); index++)
    {
        delete _arrSockets[index];
        _arrSockets[index] = NULL;
    }

    len = _threads.size();
    for (size_t index = 0; index < len; index++)
    {
        CStunSocketThread* pThread = _threads[index];
        delete pThread;
        _threads[index] = NULL;
    }
    _threads.clear();
    
    _spAuth.ReleaseAndClear();


    return S_OK;
}



HRESULT CStunServer::Start()
{
    HRESULT hr = S_OK;
    size_t len = _threads.size();

    ChkIfA(len == 0, E_UNEXPECTED);

    for (size_t index = 0; index < len; index++)
    {
        CStunSocketThread* pThread = _threads[index];
        if (pThread != NULL)
        {
            // set the "exit flag" that each thread looks at when it wakes up from waiting
            ChkA(pThread->Start());
        }
    }

Cleanup:
    if (FAILED(hr))
    {
        Stop();
    }

    return hr;
}

HRESULT CStunServer::Stop()
{


    size_t len = _threads.size();

    for (size_t index = 0; index < len; index++)
    {
        CStunSocketThread* pThread = _threads[index];
        if (pThread != NULL)
        {
            // set the "exit flag" that each thread looks at when it wakes up from waiting
            pThread->SignalForStop(false);
        }
    }


    for (size_t index = 0; index < len; index++)
    {
        CStunSocketThread* pThread = _threads[index];

        // Post a bunch of empty buffers to get the threads unblocked from whatever socket call they are on
        // In multi-threaded mode, this may wake up a different thread.  But that's ok, since all threads start and stop together
        if (pThread != NULL)
        {
            pThread->SignalForStop(true);
        }
    }

    for (size_t index = 0; index < len; index++)
    {
        CStunSocketThread* pThread = _threads[index];

        if  (pThread != NULL)
        {
            pThread->WaitForStopAndClose();
        }
    }


    return S_OK;
}




