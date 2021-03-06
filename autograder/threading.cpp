/*
 * File: threading.cpp
 * -------------------
 * This file contains implementation of code to run GoogleTest test cases
 * with a timeout, possibly in a separate thread depending on the platform.
 *
 * @author Marty Stepp
 * @version 2015/10/01
 * - fixed Mac OS X ETIMEDOUT errno.h issue; should reduce hanging
 * @version 2014/11/29
 * - fixes for Mac OS X's shitty implementation of pthread library
 * @version 2014/11/26
 * @since 2014/11/26
 */

#include "autograder.h"
#include "autogradertest.h"
#include "exceptions.h"
#include "private/platform.h"

static const std::string TIMEOUT_ERROR_MESSAGE = "test timed out! possible infinite loop";
static const std::string EXCEPTION_ERROR_MESSAGE = "test threw an exception!";

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#undef MOUSE_EVENT
#undef KEY_EVENT
#undef MOUSE_MOVED
#undef HELP_KEY

/*
 * Runs the given test case in its own thread function.
 * NOTE: Does not yet handle SIGNAL errors like segfaults.
 */
static DWORD WINAPI runTestInItsOwnThread(LPVOID lpParam) {
    autograder::AutograderTest* test = (autograder::AutograderTest*) lpParam;
    getPlatform()->autograderunittest_setTestResult(test->getName(), "progress");
    test->TestRealBody();
    return (DWORD) 0;
}

void runTestWithTimeout(autograder::AutograderTest* test) {
    int timeoutMS = test->getTestTimeout();
    if (timeoutMS > 0) {
        DWORD threadID;
        HANDLE hThread = CreateThread(
            NULL,                   // default security attributes
            0,                      // use default stack size
            runTestInItsOwnThread,  // thread function name
            (LPVOID) test,          // argument to pass to thread function
            0,                      // use default creation flags
            &threadID);             // returns the thread identifier
        if (!hThread) {
            error("Unable to run test case thread: " + getPlatform()->os_getLastError());
        }
        DWORD result = WaitForSingleObject(hThread, timeoutMS);
        if (result == WAIT_TIMEOUT) {
            TerminateThread(hThread, 1);
            autograder::setFailDetails(autograder::UnitTestDetails(
                autograder::UnitTestType::TEST_FAIL,
                TIMEOUT_ERROR_MESSAGE));
            error(TIMEOUT_ERROR_MESSAGE);
        }
    } else {
        // no timeout specified; just run the test without a thread
        test->TestRealBody();
    }
}

#else // not _WIN32
#ifdef __APPLE__
#include <errno.h>
#ifndef ETIMEDOUT
#define ETIMEDOUT 60
#endif

/*
 * I need this because Mac OS X doesn't include the function pthread_timedjoin_np.
 * This code was shamelessly stolen from:
 * http://stackoverflow.com/questions/11551188/alternative-to-pthread-timedjoin-np
 */
struct args {
    int joined;
    pthread_t td;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
    void** res;
};

static void* waiter(void* ap) {
    struct args* args = (struct args*) ap;
    pthread_join(args->td, args->res);
    pthread_mutex_lock(&args->mtx);
    pthread_mutex_unlock(&args->mtx);
    args->joined = 1;
    pthread_cond_signal(&args->cond);
    return 0;
}

int pthread_timedjoin_np(pthread_t td, void** res, struct timespec* ts) {
    pthread_t tmp;
    int ret;
    struct args args = {
        .td = td,
        .res = res
    };

    pthread_mutex_init(&args.mtx, 0);
    pthread_cond_init(&args.cond, 0);
    pthread_mutex_lock(&args.mtx);

    ret = pthread_create(&tmp, 0, waiter, &args);
    if (ret) {
        return -1;
    }

    do {
        ret = pthread_cond_timedwait(&args.cond, &args.mtx, ts);
    } while (!args.joined && ret != ETIMEDOUT);

    pthread_cancel(tmp);
    pthread_join(tmp, 0);

    pthread_cond_destroy(&args.cond);
    pthread_mutex_destroy(&args.mtx);

    return args.joined ? 0 : ETIMEDOUT;
}
#endif // __APPLE__
#include <pthread.h>
#include <sys/time.h>

/*
 * Called when test thread crashes with an exception.
 */
static void failWithException(autograder::AutograderTest* test, std::string kind, std::string desc) {
    std::ostringstream out;
    out << kind << " was thrown during test execution:" << std::endl;
    out << desc << std::endl;
    exceptions::printStackTrace(out);
    std::string errorMessage = out.str();
    stanfordcpplib::getPlatform()->autograderunittest_setTestResult(test->getName(), "fail");
    autograder::setFailDetails(autograder::UnitTestDetails(
        autograder::UnitTestType::TEST_EXCEPTION,
        errorMessage));
    pthread_exit((void*) NULL);
}

/*
 * Runs the given test case in its own thread function.
 * NOTE: Does not yet handle SIGNAL errors like segfaults.
 */
static void* runTestInItsOwnThread(void* arg) {
    autograder::AutograderTest* test = (autograder::AutograderTest*) arg;
    // getPlatform()->autograderunittest_setTestResult(test->getName(), "progress");
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    try {
        test->TestRealBody();
    } catch (const ErrorException& ex) {
        failWithException(test, "An ErrorException", ex.what());
    } catch (const std::exception& ex) {
        failWithException(test, "A C++ exception", ex.what());
    } catch (std::string str) {
        failWithException(test, "A string exception", str);
    } catch (char const* str) {
        failWithException(test, "A string exception", str);
    } catch (int n) {
        failWithException(test, "An int exception", integerToString(n));
    } catch (long l) {
        failWithException(test, "A long exception", longToString(l));
    } catch (char c) {
        failWithException(test, "A char exception", charToString(c));
    } catch (bool b) {
        failWithException(test, "A bool exception", boolToString(b));
    } catch (double d) {
        failWithException(test, "A double exception", realToString(d));
    }
    
    return (void*) NULL;
}

/*
 * Implemented using pthread library;
 * see: http://man7.org/linux/man-pages/man3/pthread_create.3.html
 */
void runTestWithTimeout(autograder::AutograderTest* test) {
    int timeoutMS = test->getTestTimeout();
    if (timeoutMS > 0) {
        // create a new pthread and run the test in that thread
        pthread_t thread;
        pthread_create(&thread, NULL, &runTestInItsOwnThread, (void*) test);

        // convert the thread's timeout in ms into the needed timeval struct
        struct timeval now;
        struct timespec timeToWait;
        gettimeofday(&now,NULL);
        timeToWait.tv_sec = now.tv_sec + (timeoutMS / 1000);
        timeToWait.tv_nsec = ((now.tv_usec + 1000UL * (timeoutMS % 1000)) * 1000UL) % 1000000UL;
        
        // wait for the given timeout amount of time
        void* threadReturn = NULL;
        int joinResult = pthread_timedjoin_np(thread, &threadReturn, &timeToWait);
        if (joinResult == ETIMEDOUT) {
            // thread didn't finish by the timeout; halt it and show failure
            pthread_cancel(thread);
            autograder::setFailDetails(autograder::UnitTestDetails(
                autograder::UnitTestType::TEST_FAIL,
                TIMEOUT_ERROR_MESSAGE));
            error(TIMEOUT_ERROR_MESSAGE);
        } else if (joinResult != 0) {
            // something went wrong, e.g. exception thrown
        }
    } else {
        // no timeout specified; just run the test without a thread
        test->TestRealBody();
    }
}

#endif // _WIN32
