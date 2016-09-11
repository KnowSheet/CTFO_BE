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
#include "../../Current/Midichlorians/Server/server.h"
#include "../../Current/Sherlock/sherlock.h"
#include "../../Current/Sherlock/replicator.h"

#include "../storage.h"

#include "cruncher_active_users.h"
#include "cruncher_top_cards.h"
#include "schema.h"

DEFINE_bool(write_ctfo_storage_golden_files, false, "Set to `true` to [over]write golden files.");
DEFINE_int32(sherlock_http_test_port,
             PickPortForUnitTest(),
             "Local port to use remote subscription unit test.");
DEFINE_int32(cruncher_http_test_port, PickPortForUnitTest(), "Local port to expose crunchers on.");

CURRENT_NAMESPACE(CTFO_Local) {
  CURRENT_NAMESPACE_TYPE(CTFOStorageTransaction, typename current::storage::transaction_t<CTFOStorage>);
  CURRENT_NAMESPACE_TYPE(CTFOLogEntry,
                         Variant<CTFOStorageTransaction, current::midichlorians::server::EventLogEntry>);
  CURRENT_NAMESPACE_TYPE(EventLogEntry, current::midichlorians::server::EventLogEntry);
  CURRENT_NAMESPACE_TYPE(iOSGenericEvent, current::midichlorians::ios::iOSGenericEvent);
  CURRENT_NAMESPACE_TYPE(iOSFocusEvent, current::midichlorians::ios::iOSFocusEvent);
  CURRENT_NAMESPACE_TYPE(iOSIdentifyEvent, current::midichlorians::ios::iOSIdentifyEvent);
  CURRENT_NAMESPACE_TYPE(iOSAppLaunchEvent, current::midichlorians::ios::iOSAppLaunchEvent);
  CURRENT_NAMESPACE_TYPE(iOSFirstLaunchEvent, current::midichlorians::ios::iOSFirstLaunchEvent);
  CURRENT_NAMESPACE_TYPE(iOSBaseEvent, current::midichlorians::ios::iOSBaseEvent);
  CURRENT_NAMESPACE_TYPE(
      Storage,
      CTFOStorage<SherlockStreamPersister, current::storage::transaction_policy::Synchronous, CTFOLogEntry>);
  CURRENT_NAMESPACE_TYPE(Sherlock, current::sherlock::Stream<CTFOLogEntry, current::persistence::File>);
  CURRENT_NAMESPACE_TYPE(Transaction_T9220981828355492272, current::storage::transaction_t<CTFOStorage>);
  CURRENT_NAMESPACE_TYPE(Persisted_CardDeleted, Persisted_CardDeleted);
  CURRENT_NAMESPACE_TYPE(CID, CTFO::CID);
};

TEST(CTFOCrunchersTest, AutogeneratedActiveUsersStorageIsUpToDate) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "active_users_db.json");
  const std::string tmp_db_file_name = current::FileSystem::GenTmpFileName();
  const auto scoped_rm_tmp_db_file = std::make_unique<current::FileSystem::ScopedRmFile>(tmp_db_file_name);

  CTFO_Local::Sherlock stream(CTFO::SchemaKey(), tmp_db_file_name);
  CTFO_Local::iOSGenericEvent seen_event;
  seen_event.event = "SEEN";
  seen_event.fields["cid"] = "fake_cid";
  seen_event.fields["token"] = "fake_token";

  uint64_t now = 0;
  uint64_t interval = 1000llu * 3600 * 3600;
  current::time::ResetToZero();

  for (uint64_t j = 0; j < 5; ++j) {
    for (uint64_t i = j * 2; i < 20; ++i) {
      current::time::SetNow(std::chrono::microseconds(now * 1000));
      CTFO_Local::EventLogEntry log_entry;
      seen_event.user_ms = std::chrono::milliseconds(now);
      seen_event.device_id = std::string("device_id_") + current::ToString(i);
      seen_event.fields["uid"] = std::string("fake_uid_") + current::ToString(i);
      log_entry.server_us = current::time::Now();
      log_entry.event = seen_event;
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

using CTFOActiveUsersResponse = CTFO::CruncherResponse<uint64_t>;

CTFOActiveUsersResponse GetActiveUsers(const std::string& url, uint64_t ind) {
  const auto response = HTTP(GET(url + "/data?i=" + current::ToString(ind)));
  EXPECT_EQ(HTTPResponseCode.OK, response.code);
  return ParseJSON<CTFOActiveUsersResponse>(response.body);
}

TEST(CTFOCrunchersTest, ActiveUsersCruncherLocalTest) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "active_users_db.json");
  CTFO_Local::Sherlock local_stream(CTFO::SchemaKey(), golden_db_file_name);

  using CTFOActiveUsersCruncher = CTFO::ActiveUsersCruncher<CTFO_Local>;

  std::vector<std::chrono::microseconds> intervals;
  for (uint32_t i = 0; i < 12; ++i) {
    intervals.push_back(std::chrono::seconds(i + 1));
  }
  for (uint32_t i = 0; i < 15; ++i) {
    intervals.push_back(std::chrono::minutes(i + 1));
  }
  intervals.push_back(std::chrono::hours(3600 * 21));

  const std::string base_url = Printf("http://localhost:%u/active_users", FLAGS_cruncher_http_test_port);
  auto activeusers_cruncher =
      std::make_unique<CTFOActiveUsersCruncher>(FLAGS_cruncher_http_test_port, "/active_users", intervals);

  {
    const auto scope = local_stream.Subscribe(*activeusers_cruncher);
    while (GetActiveUsers(base_url, 0).timestamp < current::time::Now()) {
      std::this_thread::yield();
    }
  }
  // In the last 12 seconds there were 12 users, which appeared exactly one per second.
  for (uint32_t i = 0; i < 12; ++i) {
    EXPECT_EQ(i + 1, GetActiveUsers(base_url, i).value);
  }
  // In the previous 12 minutes there were no 'new' users, only the same ones.
  for (uint32_t i = 0; i < 13; ++i) {
    EXPECT_EQ(12u, GetActiveUsers(base_url, i + 12).value);
  }
  // A minute before there was one more 'unique' active user.
  EXPECT_EQ(13u, GetActiveUsers(base_url, 25).value);
  // And a minute before that - another one.
  EXPECT_EQ(14u, GetActiveUsers(base_url, 26).value);
  // From the very beginning there were exactly 20 different active users.
  EXPECT_EQ(20u, GetActiveUsers(base_url, 27).value);
}

TEST(CTFOCrunchersTest, ActiveUsersCruncherRemoteTest) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "active_users_db.json");
  const auto schema_key = CTFO::SchemaKey();
  CTFO_Local::Sherlock local_stream(schema_key, golden_db_file_name);
  CTFO_Local::Storage storage(local_stream);
  storage.ExposeRawLogViaHTTP(FLAGS_sherlock_http_test_port, "/raw_log");

  current::sherlock::SubscribableRemoteStream<CTFO_2016_08_01::CTFOLogEntry> remote_stream(
      Printf("http://localhost:%d/raw_log", FLAGS_sherlock_http_test_port),
      schema_key.top_level_name,
      schema_key.namespace_name);

  using CTFOActiveUsersCruncher = CTFO::ActiveUsersCruncher<CTFO_2016_08_01>;

  std::vector<std::chrono::microseconds> intervals;
  for (uint32_t i = 0; i < 12; ++i) {
    intervals.push_back(std::chrono::seconds(i + 1));
  }
  for (uint32_t i = 0; i < 15; ++i) {
    intervals.push_back(std::chrono::minutes(i + 1));
  }
  intervals.push_back(std::chrono::hours(3600 * 21));

  const std::string base_url = Printf("http://localhost:%u/active_users", FLAGS_cruncher_http_test_port);
  auto activeusers_cruncher =
      std::make_unique<CTFOActiveUsersCruncher>(FLAGS_cruncher_http_test_port, "/active_users", intervals);

  {
    const auto scope = remote_stream.Subscribe(*activeusers_cruncher);
    while (GetActiveUsers(base_url, 0).timestamp < current::time::Now()) {
      std::this_thread::yield();
    }
  }
  // In the last 12 seconds there were 12 users, which appeared exactly one per second.
  for (uint32_t i = 0; i < 12; ++i) {
    EXPECT_EQ(i + 1, GetActiveUsers(base_url, i).value);
  }
  // In the previous 12 minutes there were no 'new' users, only the same ones.
  for (uint32_t i = 0; i < 13; ++i) {
    EXPECT_EQ(12u, GetActiveUsers(base_url, i + 12).value);
  }
  // A minute before there was one more 'unique' active user.
  EXPECT_EQ(13u, GetActiveUsers(base_url, 25).value);
  // And a minute before that - another one.
  EXPECT_EQ(14u, GetActiveUsers(base_url, 26).value);
  // From the very beginning there were exactly 20 different active users.
  EXPECT_EQ(20u, GetActiveUsers(base_url, 27).value);
}

TEST(CTFOCrunchersTest, AutogeneratedTopCardsStorageIsUpToDate) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "top_cards_db.json");
  const std::string tmp_db_file_name = current::FileSystem::GenTmpFileName();
  const auto scoped_rm_tmp_db_file = std::make_unique<current::FileSystem::ScopedRmFile>(tmp_db_file_name);

  CTFO_Local::Sherlock stream(CTFO::SchemaKey(), tmp_db_file_name);
  CTFO_Local::iOSGenericEvent ios_event;
  ios_event.fields["uid"] = "fake_uid";
  ios_event.fields["token"] = "fake_token";
  ios_event.device_id = std::string("fake_device_id");

  uint64_t now = 0;
  uint64_t interval = 1000llu * 3600 * 3600;
  current::time::ResetToZero();

  const std::string events[] = {"SEEN", "CTFO", "SKIP", "TFU", "FAV"};
  for (uint64_t j = 0; j < 5; ++j) {
    for (uint64_t i = j * 2; i < 20; ++i) {
      for (uint64_t k = 0, cnt = 20 - i; k < cnt; ++k) {
        current::time::SetNow(std::chrono::microseconds(now * 1000 + 20 - cnt + k));
        CTFO_Local::EventLogEntry log_entry;
        ios_event.event = events[j];
        ios_event.user_ms = std::chrono::milliseconds(now);
        ios_event.fields["cid"] = CTFO::CIDToString(static_cast<CTFO::CID>(i));
        log_entry.server_us = current::time::Now();
        log_entry.event = ios_event;
        stream.Publish(CTFO_Local::CTFOLogEntry(log_entry));
      }
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

using CTFOTopCardsResponse = CTFO::CruncherResponse<CTFO::TopCardsCruncherResponse>;

CTFOTopCardsResponse GetTopCards(const std::string& url, uint64_t ind) {
  const auto response = HTTP(GET(url + "/data?i=" + current::ToString(ind)));
  EXPECT_EQ(HTTPResponseCode.OK, response.code);
  return ParseJSON<CTFOTopCardsResponse>(response.body);
}

TEST(CTFOCrunchersTest, TopCardsCruncherLocalTest) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "top_cards_db.json");
  CTFO_Local::Sherlock local_stream(CTFO::SchemaKey(), golden_db_file_name);

  using CTFOTopCardsCruncher = CTFO::TopCardsCruncher<CTFO_Local>;

  std::vector<CTFO::TopCardsCruncherArgs> args;
  const auto calculator =
      [](uint64_t ctfo, uint64_t skip, uint64_t tfu, uint64_t fav, uint64_t unfav, uint64_t seen)
          -> int64_t { return 160000 * seen + 8000 * ctfo + 400 * skip + 20 * tfu + fav - unfav; };
  for (uint32_t i = 0; i < 12; ++i) {
    args.emplace_back(std::chrono::seconds(i + 1), 20, calculator);
  }
  for (uint32_t i = 0; i < 15; ++i) {
    args.emplace_back(std::chrono::minutes(i + 1), 20, calculator);
  }
  args.emplace_back(std::chrono::hours(3600 * 21), 20, calculator);

  const std::string base_url = Printf("http://localhost:%u/top_cards", FLAGS_cruncher_http_test_port);
  auto top_cards_cruncher =
      std::make_unique<CTFOTopCardsCruncher>(FLAGS_cruncher_http_test_port, "/top_cards", args);

  {
    const auto scope = local_stream.Subscribe(*top_cards_cruncher);
    while (GetTopCards(base_url, 0).timestamp < current::time::Now()) {
      std::this_thread::yield();
    }
  }

  for (uint32_t i = 0; i < 12; ++i) {
    const auto top_cards = GetTopCards(base_url, i).value;
    EXPECT_EQ(i + 1, top_cards.size());
    for (uint32_t j = 0; j < top_cards.size(); ++j) {
      EXPECT_EQ(19 - i + j, top_cards[j].cid);
      EXPECT_EQ(i + 1 - j, top_cards[j].rate);
    }
  }
  for (uint32_t i = 0; i < 12; ++i) {
    const auto top_cards = GetTopCards(base_url, i + 12).value;
    EXPECT_EQ(12u, top_cards.size());
    for (uint32_t j = 0; j < i; ++j) {
      EXPECT_EQ(20 - (i - j), top_cards[j].cid);
      EXPECT_EQ((i - j) * 21, top_cards[j].rate);
    }
    for (uint32_t j = i; j < top_cards.size(); ++j) {
      EXPECT_EQ(8 + (j - i), top_cards[j].cid);
      EXPECT_EQ(12 - (j - i), top_cards[j].rate);
    }
  }
  for (uint32_t i = 0; i < 2; ++i) {
    const auto top_cards = GetTopCards(base_url, i + 25).value;
    EXPECT_EQ(i + 13, top_cards.size());
    for (uint32_t j = 0; j < i + 1; ++j) {
      EXPECT_EQ(7 - (i - j), top_cards[j].cid);
      EXPECT_EQ((i - j + 13) * 20, top_cards[j].rate);
    }
    for (uint32_t j = i + 1; j < top_cards.size(); ++j) {
      EXPECT_EQ(7 + (j - i), top_cards[j].cid);
      EXPECT_EQ((13 - (j - i)) * 21, top_cards[j].rate);
    }
  }
  {
    const auto top_cards = GetTopCards(base_url, 27).value;
    EXPECT_EQ(20u, top_cards.size());
    for (uint32_t i = 0; i < top_cards.size(); ++i) {
      EXPECT_EQ(i, top_cards[i].cid);
      const uint32_t mult = 20 - i;
      if (i < 2) {
        EXPECT_EQ(mult * 160000, top_cards[i].rate);
      } else if (i < 4) {
        EXPECT_EQ(mult * (160000 + 8000), top_cards[i].rate);
      } else if (i < 6) {
        EXPECT_EQ(mult * (160000 + 8000 + 400), top_cards[i].rate);
      } else if (i < 8) {
        EXPECT_EQ(mult * (160000 + 8000 + 400 + 20), top_cards[i].rate);
      } else {
        EXPECT_EQ(mult * (160000 + 8000 + 400 + 20 + 1), top_cards[i].rate);
      }
    }
  }
}

TEST(CTFOCrunchersTest, TopCardsCruncherRemoteTest) {
  const std::string golden_db_file_name = current::FileSystem::JoinPath("golden", "top_cards_db.json");
  const auto schema_key = CTFO::SchemaKey();
  CTFO_Local::Sherlock local_stream(schema_key, golden_db_file_name);
  CTFO_Local::Storage storage(local_stream);
  storage.ExposeRawLogViaHTTP(FLAGS_sherlock_http_test_port, "/raw_log");

  current::sherlock::SubscribableRemoteStream<CTFO_2016_08_01::CTFOLogEntry> remote_stream(
      Printf("http://localhost:%d/raw_log", FLAGS_sherlock_http_test_port),
      schema_key.top_level_name,
      schema_key.namespace_name);

  using CTFOTopCardsCruncher = CTFO::TopCardsCruncher<CTFO_2016_08_01>;

  std::vector<CTFO::TopCardsCruncherArgs> args;
  const auto calculator =
      [](uint64_t ctfo, uint64_t skip, uint64_t tfu, uint64_t fav, uint64_t unfav, uint64_t seen)
          -> int64_t { return 160000 * seen + 8000 * ctfo + 400 * skip + 20 * tfu + fav - unfav; };
  for (uint32_t i = 0; i < 12; ++i) {
    args.emplace_back(std::chrono::seconds(i + 1), 20, calculator);
  }
  for (uint32_t i = 0; i < 15; ++i) {
    args.emplace_back(std::chrono::minutes(i + 1), 20, calculator);
  }
  args.emplace_back(std::chrono::hours(3600 * 21), 20, calculator);

  const std::string base_url = Printf("http://localhost:%u/top_cards", FLAGS_cruncher_http_test_port);
  auto top_cards_cruncher =
      std::make_unique<CTFOTopCardsCruncher>(FLAGS_cruncher_http_test_port, "/top_cards", args);

  {
    const auto scope = remote_stream.Subscribe(*top_cards_cruncher);
    while (GetTopCards(base_url, 0).timestamp < current::time::Now()) {
      std::this_thread::yield();
    }
  }

  for (uint32_t i = 0; i < 12; ++i) {
    const auto top_cards = GetTopCards(base_url, i).value;
    EXPECT_EQ(i + 1, top_cards.size());
    for (uint32_t j = 0; j < top_cards.size(); ++j) {
      EXPECT_EQ(19 - i + j, top_cards[j].cid);
      EXPECT_EQ(i + 1 - j, top_cards[j].rate);
    }
  }
  for (uint32_t i = 0; i < 12; ++i) {
    const auto top_cards = GetTopCards(base_url, i + 12).value;
    EXPECT_EQ(12u, top_cards.size());
    for (uint32_t j = 0; j < i; ++j) {
      EXPECT_EQ(20 - (i - j), top_cards[j].cid);
      EXPECT_EQ((i - j) * 21, top_cards[j].rate);
    }
    for (uint32_t j = i; j < top_cards.size(); ++j) {
      EXPECT_EQ(8 + (j - i), top_cards[j].cid);
      EXPECT_EQ(12 - (j - i), top_cards[j].rate);
    }
  }
  for (uint32_t i = 0; i < 2; ++i) {
    const auto top_cards = GetTopCards(base_url, i + 25).value;
    EXPECT_EQ(i + 13, top_cards.size());
    for (uint32_t j = 0; j < i + 1; ++j) {
      EXPECT_EQ(7 - (i - j), top_cards[j].cid);
      EXPECT_EQ((i - j + 13) * 20, top_cards[j].rate);
    }
    for (uint32_t j = i + 1; j < top_cards.size(); ++j) {
      EXPECT_EQ(7 + (j - i), top_cards[j].cid);
      EXPECT_EQ((13 - (j - i)) * 21, top_cards[j].rate);
    }
  }
  {
    const auto top_cards = GetTopCards(base_url, 27).value;
    EXPECT_EQ(20u, top_cards.size());
    for (uint32_t i = 0; i < top_cards.size(); ++i) {
      EXPECT_EQ(i, top_cards[i].cid);
      const uint32_t mult = 20 - i;
      if (i < 2) {
        EXPECT_EQ(mult * 160000, top_cards[i].rate);
      } else if (i < 4) {
        EXPECT_EQ(mult * (160000 + 8000), top_cards[i].rate);
      } else if (i < 6) {
        EXPECT_EQ(mult * (160000 + 8000 + 400), top_cards[i].rate);
      } else if (i < 8) {
        EXPECT_EQ(mult * (160000 + 8000 + 400 + 20), top_cards[i].rate);
      } else {
        EXPECT_EQ(mult * (160000 + 8000 + 400 + 20 + 1), top_cards[i].rate);
      }
    }
  }
}

#endif  // CTFO_CRUNCHERS_TESTS
