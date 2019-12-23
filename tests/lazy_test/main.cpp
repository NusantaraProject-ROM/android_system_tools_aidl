/*
 * Copyright (C) 2019, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>

#include <unistd.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <gtest/gtest.h>
#include <utils/String8.h>

using ::android::IBinder;
using ::android::IPCThreadState;
using ::android::IServiceManager;
using ::android::sp;

static android::String16 gServiceName;

sp<IBinder> waitForService() {
  sp<IServiceManager> manager;
  manager = android::defaultServiceManager();
  EXPECT_TRUE(manager != nullptr);

  return manager->waitForService(gServiceName);
}

class AidlLazyTest : public ::testing::Test {
 protected:
  sp<IServiceManager> manager;

  void SetUp() override {
    manager = android::defaultServiceManager();
    ASSERT_NE(manager, nullptr);

    ASSERT_FALSE(isServiceRunning())
        << "Service '" << gServiceName << "' is already running. Please ensure this "
        << "is implemented as a lazy service, then kill all "
        << "clients of this service and try again.";
  }

  static constexpr size_t SHUTDOWN_WAIT_TIME = 10;
  void TearDown() override {
    std::cout << "Waiting " << SHUTDOWN_WAIT_TIME << " seconds before checking that the "
              << "service has shut down." << std::endl;
    IPCThreadState::self()->flushCommands();
    sleep(SHUTDOWN_WAIT_TIME);
    ASSERT_FALSE(isServiceRunning()) << "Service failed to shut down.";
  }

  bool isServiceRunning() {
    auto services = manager->listServices();
    for (size_t i = 0; i < services.size(); i++) {
      if (services[i] == gServiceName) return true;
    }
    return false;
  }
};

static constexpr size_t NUM_IMMEDIATE_GETS = 100;
TEST_F(AidlLazyTest, GetRelease) {
  for (size_t i = 0; i < NUM_IMMEDIATE_GETS; i++) {
    IPCThreadState::self()->flushCommands();
    sp<IBinder> service = waitForService();
    ASSERT_NE(service.get(), nullptr);
    EXPECT_TRUE(service->pingBinder() == android::NO_ERROR);
  }
}

static std::vector<size_t> waitTimes(size_t numTimes, size_t maxWait) {
  std::vector<size_t> times(numTimes);
  for (size_t i = 0; i < numTimes; i++) {
    times[i] = (size_t)(rand() % (maxWait + 1));
  }
  return times;
}

static void testWithTimes(const std::vector<size_t>& waitTimes, bool beforeGet) {
  for (size_t sleepTime : waitTimes) {
    IPCThreadState::self()->flushCommands();
    if (beforeGet) {
      std::cout << "Thread waiting " << sleepTime << " while not holding service." << std::endl;
      sleep(sleepTime);
    }

    sp<IBinder> service = waitForService();

    if (!beforeGet) {
      std::cout << "Thread waiting " << sleepTime << " while holding service." << std::endl;
      sleep(sleepTime);
    }

    ASSERT_NE(service.get(), nullptr);
    ASSERT_TRUE(service->pingBinder() == android::NO_ERROR);
  }
}

static constexpr size_t NUM_TIMES_GET_RELEASE = 5;
static constexpr size_t MAX_WAITING_DURATION = 10;
static constexpr size_t NUM_CONCURRENT_THREADS = 5;
static void testConcurrentThreadsWithDelays(bool delayBeforeGet) {
  std::vector<std::vector<size_t>> threadWaitTimes(NUM_CONCURRENT_THREADS);

  int maxWait = 0;
  for (size_t i = 0; i < threadWaitTimes.size(); i++) {
    threadWaitTimes[i] = waitTimes(NUM_TIMES_GET_RELEASE, MAX_WAITING_DURATION);
    int totalWait = std::accumulate(threadWaitTimes[i].begin(), threadWaitTimes[i].end(), 0);
    maxWait = std::max(maxWait, totalWait);
  }
  std::cout << "Additional runtime expected from sleeps: " << maxWait << " second(s)." << std::endl;

  std::vector<std::thread> threads(NUM_CONCURRENT_THREADS);
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i] = std::thread(testWithTimes, threadWaitTimes[i], delayBeforeGet);
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(AidlLazyTest, GetConcurrentWithWaitBefore) {
  testConcurrentThreadsWithDelays(true);
}

TEST_F(AidlLazyTest, GetConcurrentWithWaitAfter) {
  testConcurrentThreadsWithDelays(false);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  srand(time(nullptr));

  if (argc != 2) {
    std::cerr << "Usage: aidl_lazy_test serviceName" << std::endl;
    return 1;
  }

  gServiceName = android::String16(argv[1]);

  android::ProcessState::self()->startThreadPool();

  return RUN_ALL_TESTS();
}
