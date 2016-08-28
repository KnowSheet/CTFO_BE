/*******************************************************************************
 The MIT License (MIT)

 Copyright (c) 2016 Grigory Nikolaenko <nikolaenko.grigory@gmail.com>

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 *******************************************************************************/

#ifndef CTFO_CRUNCHERS_TESTS
#define CTFO_CRUNCHERS_TESTS

#define CURRENT_MOCK_TIME  // `SetNow()`.

#include "../../Current/Bricks/dflags/dflags.h"
#include "../../Current/3rdparty/gtest/gtest-main-with-dflags.h"
#include "../../Current/Sherlock/sherlock.h"
#include "../../Current/Sherlock/replicator.h"

#include "../ctfo_server.h"

#include "cruncher_active_users.h"
#include "schema.h"

DEFINE_bool(write_ctfo_storage_golden_files, false, "Set to `true` to [over]write golden files.");
DEFINE_int32(sherlock_http_test_port,
             PickPortForUnitTest(),
             "Local port to use remote subscription unit test.");

CURRENT_NAMESPACE(CTFO_Local) {
  CURRENT_NAMESPACE_TYPE(CTFOLogEntry, CTFO::CTFOLogEntry);
  CURRENT_NAMESPACE_TYPE(EventLogEntry, current::midichlorians::server::EventLogEntry);
  CURRENT_NAMESPACE_TYPE(iOSGenericEvent, current::midichlorians::ios::iOSGenericEvent);
  CURRENT_NAMESPACE_TYPE(iOSFocusEvent, current::midichlorians::ios::iOSFocusEvent);
  CURRENT_NAMESPACE_TYPE(iOSIdentifyEvent, current::midichlorians::ios::iOSIdentifyEvent);
  CURRENT_NAMESPACE_TYPE(iOSAppLaunchEvent, current::midichlorians::ios::iOSAppLaunchEvent);
  CURRENT_NAMESPACE_TYPE(iOSFirstLaunchEvent, current::midichlorians::ios::iOSFirstLaunchEvent);
  CURRENT_NAMESPACE_TYPE(iOSBaseEvent, current::midichlorians::ios::iOSBaseEvent);
};

TEST(CTFOCrunchersTest, AutogeneratedStorageIsUpToDate) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "active_users_db.json");
  const std::string tmp_db_file_name = current::FileSystem::GenTmpFileName();
  const auto scoped_rm_tmp_db_file = std::make_unique<current::FileSystem::ScopedRmFile>(tmp_db_file_name);

  CTFO::CTFOServer::stream_t stream(CTFO::SchemaKey(), tmp_db_file_name);
  CTFO_Local::iOSGenericEvent favorite_event;
  favorite_event.event = "SEEN";
  favorite_event.fields["cid"] = "fake_cid";
  favorite_event.fields["token"] = "fake_token";

  uint64_t now = 0;
  uint64_t interval = 1000llu * 3600 * 3600;
  current::time::ResetToZero();

  for (uint64_t j = 0; j < 5; ++j) {
    for (uint64_t i = j * 2; i < 20; ++i) {
      current::time::SetNow(std::chrono::microseconds(now * 1000));
      CTFO_Local::EventLogEntry log_entry;
      favorite_event.user_ms = std::chrono::milliseconds(now);
      favorite_event.device_id = std::string("device_id_") + current::ToString(i);
      favorite_event.fields["uid"] = std::string("fake_uid_") + current::ToString(i);
      log_entry.server_us = current::time::Now();
      log_entry.event = favorite_event;
      stream.Publish(CTFO_Local::CTFOLogEntry(log_entry));
      now += interval;
    }
    interval /= 60;
  }
  const std::string golden = current::FileSystem::ReadFileAsString(tmp_db_file_name);

  if (!FLAGS_write_ctfo_storage_golden_files) {
    EXPECT_EQ(current::FileSystem::ReadFileAsString(golden_db_file_name), golden);
  } else {
    current::FileSystem::WriteStringToFile(golden, golden_db_file_name.c_str());
  }
}

TEST(CTFOCrunchersTest, ActiveUsersCruncherLocalTest) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "active_users_db.json");
  CTFO::CTFOServer::stream_t local_stream(CTFO::SchemaKey(), golden_db_file_name);

  using CTFOActiveUsersCruncher = CTFO::ActiveUsersCruncher<CTFO_Local>;

  std::unique_ptr<CTFOActiveUsersCruncher> activeusers_cruncher;

  const auto SubscribeAndWait = [&]() {
    const auto scope = local_stream.Subscribe(*activeusers_cruncher);
    while (activeusers_cruncher->Size() < 80) {
      std::this_thread::yield();
    }
  };

  // In the last 12 seconds there were 12 users, which appeaed exactly one per second.
  for (uint32_t i = 1; i <= 12; ++i) {
    activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::seconds(i));
    SubscribeAndWait();
    EXPECT_EQ(i, activeusers_cruncher->Count());
  }

  // In the previous 12 minutes there were no 'new' users, only the same ones.
  for (uint32_t i = 1; i <= 13; ++i) {
    activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::minutes(i));
    SubscribeAndWait();
    EXPECT_EQ(12u, activeusers_cruncher->Count());
  }

  // A minute berfore there was one more 'unique' active user.
  activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::minutes(14));
  SubscribeAndWait();
  EXPECT_EQ(13u, activeusers_cruncher->Count());

  // And a minute berfore that - another one.
  activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::minutes(15));
  SubscribeAndWait();
  EXPECT_EQ(14u, activeusers_cruncher->Count());

  // From the very beginning there were exactly 20 different active users.
  activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::hours(3600 * 21));
  SubscribeAndWait();
  EXPECT_EQ(20u, activeusers_cruncher->Count());
}

TEST(CTFOCrunchersTest, ActiveUsersCruncherRemoteTest) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "active_users_db.json");
  const auto schema_key = CTFO::SchemaKey();
  CTFO::CTFOServer::stream_t local_stream(schema_key, golden_db_file_name);
  CTFO::CTFOServer::storage_t storage(local_stream);
  storage.ExposeRawLogViaHTTP(FLAGS_sherlock_http_test_port, "/raw_log");

  current::sherlock::SubscribableRemoteStream<CTFO_2016_08_01::CTFOLogEntry> remote_stream(
      Printf("http://localhost:%d/raw_log", FLAGS_sherlock_http_test_port),
      schema_key.top_level_name,
      schema_key.namespace_name);

  using CTFOActiveUsersCruncher = CTFO::ActiveUsersCruncher<CTFO_2016_08_01>;

  auto activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::seconds(1u));

  const auto SubscribeAndWait = [&]() {
    const auto scope = remote_stream.Subscribe(*activeusers_cruncher);
    while (activeusers_cruncher->Size() < 80) {
      std::this_thread::yield();
    }
  };

  // In the last 12 seconds there were 12 users, which appeaed exactly one per second.
  for (uint32_t i = 1; i <= 12; ++i) {
    activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::seconds(i));
    SubscribeAndWait();
    EXPECT_EQ(i, activeusers_cruncher->Count());
  }
  
  // In the previous 12 minutes there were no 'new' users, only the same ones.
  for (uint32_t i = 1; i <= 13; ++i) {
    activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::minutes(i));
    SubscribeAndWait();
    EXPECT_EQ(12u, activeusers_cruncher->Count());
  }
  
  // A minute berfore there was one more 'unique' active user.
  activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::minutes(14));
  SubscribeAndWait();
  EXPECT_EQ(13u, activeusers_cruncher->Count());
  
  // And a minute berfore that - another one.
  activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::minutes(15));
  SubscribeAndWait();
  EXPECT_EQ(14u, activeusers_cruncher->Count());
  
  // From the very beginning there were exactly 20 different active users.
  activeusers_cruncher = std::make_unique<CTFOActiveUsersCruncher>(std::chrono::hours(3600 * 21));
  SubscribeAndWait();
  EXPECT_EQ(20u, activeusers_cruncher->Count());
}

#endif  // CTFO_CRUNCHERS_TESTS
