/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2018, Live Networks, Inc.  All rights reserved

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);

void subsessionAfterPlaying(void* clientData);
void subsessionByeHandler(void* clientData);
void streamTimerHandler(void* clientData);

void openURL(UsageEnvironment& env, char const* progName, char const* rtspURL);

void setupNextSubsession(RTSPClient* rtspClient);

void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);

char eventLoopWatchVariable = 0;

int main(int argc, char** argv) {
    TaskScheduler* scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

    if (argc != 2)
        return 1;

    openURL(*env, argv[0], argv[1]);

    env->taskScheduler().doEventLoop(&eventLoopWatchVariable);

    return 0;
}

class StreamClientState {
public:
    StreamClientState();
    virtual ~StreamClientState();

public:
    MediaSubsessionIterator* iter;
    MediaSession* session;
    MediaSubsession* subsession;
    TaskToken streamTimerTask;
    double duration;
};

class ourRTSPClient: public RTSPClient {
public:
    static ourRTSPClient* createNew(UsageEnvironment& env, char const* rtspURL,
				    int verbosityLevel = 0,
				    char const* applicationName = NULL,
				    portNumBits tunnelOverHTTPPortNum = 0);

protected:
    ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
		int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum);

    virtual ~ourRTSPClient();

public:
    StreamClientState scs;
};

class DummySink: public MediaSink {
public:
    static DummySink* createNew(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId = NULL);

private:
    DummySink(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId);

    virtual ~DummySink();

    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                unsigned numTruncatedBytes,
				                struct timeval presentationTime,
                                unsigned durationInMicroseconds);
    void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
			    struct timeval presentationTime, unsigned durationInMicroseconds);

private:
    virtual Boolean continuePlaying();

private:
    u_int8_t* fReceiveBuffer;
    MediaSubsession& fSubsession;
    char* fStreamId;
};

#define RTSP_CLIENT_VERBOSITY_LEVEL 1

static unsigned rtspClientCount = 0;

void openURL(UsageEnvironment& env, char const* progName, char const* rtspURL) {
    RTSPClient* rtspClient = ourRTSPClient::createNew(env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL, progName);

    if (rtspClient == NULL)
        return;

    ++rtspClientCount;

    rtspClient->sendDescribeCommand(continueAfterDESCRIBE); 
}

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
    do {
        UsageEnvironment& env = rtspClient->envir();
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;

        if(resultCode != 0) {
            delete[] resultString;
            break;
        }

        char* const sdpDescription = resultString;

        scs.session = MediaSession::createNew(env, sdpDescription);
        delete[] sdpDescription;
        if(scs.session == NULL) {
            break;
        } else if(!scs.session->hasSubsessions()) {
            break;
        }

        scs.iter = new MediaSubsessionIterator(*scs.session);
        setupNextSubsession(rtspClient);
        return;
    } while(0);

    shutdownStream(rtspClient);
}

void setupNextSubsession(RTSPClient* rtspClient) {
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;
  
    scs.subsession = scs.iter->next();
    if(scs.subsession != NULL) {
        if(!scs.subsession->initiate()) {
            setupNextSubsession(rtspClient);
        } else {
            rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, false);
        }
        return;
    }

    if(scs.session->absStartTime() != NULL) {
        rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY, scs.session->absStartTime(), scs.session->absEndTime());
    } else {
        scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
        rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
    }
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
    do {
        UsageEnvironment& env = rtspClient->envir();
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;

        if(resultCode != 0)
            break;

        scs.subsession->sink = DummySink::createNew(env, *scs.subsession, rtspClient->url());

        if(scs.subsession->sink == NULL)
            break;

        scs.subsession->miscPtr = rtspClient;
        scs.subsession->sink->startPlaying(*(scs.subsession->readSource()), subsessionAfterPlaying, scs.subsession);

        if(scs.subsession->rtcpInstance() != NULL) {
            scs.subsession->rtcpInstance()->setByeHandler(subsessionByeHandler, scs.subsession);
        }
    } while(0);
    delete[] resultString;

    setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
    Boolean success = False;

    do {
        UsageEnvironment& env = rtspClient->envir();
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;

        if(resultCode != 0)
            break;

        if (scs.duration > 0) {
            unsigned const delaySlop = 2;
            scs.duration += delaySlop;
            unsigned uSecsToDelay = (unsigned)(scs.duration*1000000);
            scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(uSecsToDelay, (TaskFunc*)streamTimerHandler, rtspClient);
        }

        success = True;
    } while(0);
    delete[] resultString;

    if (!success)
        shutdownStream(rtspClient);
}

void subsessionAfterPlaying(void* clientData) {
    MediaSubsession* subsession = (MediaSubsession*)clientData;
    RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

    Medium::close(subsession->sink);
    subsession->sink = NULL;

    MediaSession& session = subsession->parentSession();
    MediaSubsessionIterator iter(session);
    while ((subsession = iter.next()) != NULL) {
        if (subsession->sink != NULL) return;
    }

    shutdownStream(rtspClient);
}

void subsessionByeHandler(void* clientData) {
    MediaSubsession* subsession = (MediaSubsession*)clientData;

    subsessionAfterPlaying(subsession);
}

void streamTimerHandler(void* clientData) {
    ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
    StreamClientState& scs = rtspClient->scs;

    scs.streamTimerTask = NULL;

    shutdownStream(rtspClient);
}

void shutdownStream(RTSPClient* rtspClient, int exitCode) {
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;

    if(scs.session != NULL) { 
        Boolean someSubsessionsWereActive = False;
        MediaSubsessionIterator iter(*scs.session);
        MediaSubsession* subsession;

        while((subsession = iter.next()) != NULL) {
            if(subsession->sink != NULL) {
	            Medium::close(subsession->sink);
	            subsession->sink = NULL;

	            if(subsession->rtcpInstance() != NULL)
	                subsession->rtcpInstance()->setByeHandler(NULL, NULL);	            

	            someSubsessionsWereActive = True;
            }
        }

        if(someSubsessionsWereActive)
            rtspClient->sendTeardownCommand(*scs.session, NULL);
    }

    Medium::close(rtspClient);

    if (--rtspClientCount == 0)
        exit(exitCode);
}

ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL,
					int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum) {
    return new ourRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
			        int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum)
    : RTSPClient(env,rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1) {
}

ourRTSPClient::~ourRTSPClient() {
}

StreamClientState::StreamClientState() : iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), duration(0.0) {
}

StreamClientState::~StreamClientState() {
    delete iter;
    if(session != NULL) {
        UsageEnvironment& env = session->envir();

        env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
        Medium::close(session);
    }
}

#define DUMMY_SINK_RECEIVE_BUFFER_SIZE 100000

DummySink* DummySink::createNew(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId) {
    return new DummySink(env, subsession, streamId);
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId)
    : MediaSink(env), fSubsession(subsession) {
    fStreamId = strDup(streamId);
    fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
}

DummySink::~DummySink() {
    delete[] fReceiveBuffer;
    delete[] fStreamId;
}

void DummySink::afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
				    struct timeval presentationTime, unsigned durationInMicroseconds) {
    DummySink* sink = (DummySink*)clientData;

    sink->continuePlaying();
}

Boolean DummySink::continuePlaying() {
    if (fSource == NULL) return False;

    fSource->getNextFrame(fReceiveBuffer, DUMMY_SINK_RECEIVE_BUFFER_SIZE, afterGettingFrame, this, onSourceClosure, this);
    return True;
}
