#include <aho_corasick/aho_corasick.hpp>
#include <chrono>
#include <exception>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ios>
#include <thread>

#include "common/activity/rate_observer.hpp"

TEST(RateObserverTest, SimpleLimit) {
  activity::rate_observer<std::chrono::seconds, int> observer(
      std::chrono::seconds(2), 2);
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_EQ(observer.observe_and_get_excess(), 1);
  EXPECT_EQ(observer.observe_and_get_excess(), 2);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_EQ(observer.observe_and_get_excess(), 2);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_EQ(observer.observe_and_get_excess(), 1);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
}

TEST(RateObserverTest, SlidingLimit) {
  activity::rate_observer<std::chrono::milliseconds, int> observer(
      std::chrono::milliseconds(500), 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(observer.observe_and_get_excess(), 1);
}

TEST(RateObserverTest, Overlap) {
  activity::rate_observer<std::chrono::microseconds, int> observer(
      std::chrono::microseconds(100000), 1);
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
  std::this_thread::sleep_for(std::chrono::microseconds(200000));
  EXPECT_EQ(observer.observe_and_get_excess(), 0);
  std::this_thread::sleep_for(std::chrono::microseconds(50000));
  EXPECT_EQ(observer.observe_and_get_excess(), 1);
  std::this_thread::sleep_for(std::chrono::microseconds(75000));
  EXPECT_EQ(observer.observe_and_get_excess(), 1);
}
