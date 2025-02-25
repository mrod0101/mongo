/**
 *    Copyright (C) 2022-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest

#include "mongo/platform/basic.h"

#include "mongo/db/s/sharding_data_transform_cumulative_metrics.h"
#include "mongo/db/s/sharding_data_transform_metrics_test_fixture.h"
#include "mongo/platform/random.h"
#include "mongo/stdx/thread.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/future.h"
#include "mongo/util/static_immortal.h"

namespace mongo {
namespace {

class ScopedObserverMock {
public:
    using Ptr = std::unique_ptr<ScopedObserverMock>;

    ScopedObserverMock(int64_t startTime,
                       int64_t timeRemaining,
                       ShardingDataTransformCumulativeMetrics* parent)
        : _mock{startTime, timeRemaining}, _deregister{parent->registerInstanceMetrics(&_mock)} {}

    ~ScopedObserverMock() {
        if (_deregister) {
            _deregister();
        }
    }

private:
    ObserverMock _mock;
    ShardingDataTransformCumulativeMetrics::DeregistrationFunction _deregister;
};

class ShardingDataTransformCumulativeMetricsTest : public ShardingDataTransformMetricsTestFixture {
};

TEST_F(ShardingDataTransformCumulativeMetricsTest, AddAndRemoveMetrics) {
    auto deregister = _cumulativeMetrics.registerInstanceMetrics(getOldestObserver());
    ASSERT_EQ(_cumulativeMetrics.getObservedMetricsCount(), 1);
    deregister();
    ASSERT_EQ(_cumulativeMetrics.getObservedMetricsCount(), 0);
}

TEST_F(ShardingDataTransformCumulativeMetricsTest, MetricsReportsOldestWhenInsertedFirst) {
    auto deregisterOldest = _cumulativeMetrics.registerInstanceMetrics(getOldestObserver());
    auto deregisterYoungest = _cumulativeMetrics.registerInstanceMetrics(getYoungestObserver());
    ASSERT_EQ(_cumulativeMetrics.getOldestOperationRemainingTimeMillis(), kOldestTime);
}

TEST_F(ShardingDataTransformCumulativeMetricsTest, MetricsReportsOldestWhenInsertedLast) {
    auto deregisterYoungest = _cumulativeMetrics.registerInstanceMetrics(getYoungestObserver());
    auto deregisterOldest = _cumulativeMetrics.registerInstanceMetrics(getOldestObserver());
    ASSERT_EQ(_cumulativeMetrics.getOldestOperationRemainingTimeMillis(), kOldestTime);
}

TEST_F(ShardingDataTransformCumulativeMetricsTest, RemainingTimeReports0WhenEmpty) {
    ASSERT_EQ(_cumulativeMetrics.getObservedMetricsCount(), 0);
    ASSERT_EQ(_cumulativeMetrics.getOldestOperationRemainingTimeMillis(), 0);
}

TEST_F(ShardingDataTransformCumulativeMetricsTest, UpdatesOldestWhenOldestIsRemoved) {
    auto deregisterYoungest = _cumulativeMetrics.registerInstanceMetrics(getYoungestObserver());
    auto deregisterOldest = _cumulativeMetrics.registerInstanceMetrics(getOldestObserver());
    ASSERT_EQ(_cumulativeMetrics.getOldestOperationRemainingTimeMillis(), kOldestTime);
    deregisterOldest();
    ASSERT_EQ(_cumulativeMetrics.getOldestOperationRemainingTimeMillis(), kYoungestTime);
}

TEST_F(ShardingDataTransformCumulativeMetricsTest, InsertsTwoWithSameStartTime) {
    auto deregisterOldest = _cumulativeMetrics.registerInstanceMetrics(getOldestObserver());
    ObserverMock sameAsOldest{kOldestTime, kOldestTime};
    auto deregisterOldest2 = _cumulativeMetrics.registerInstanceMetrics(&sameAsOldest);
    ASSERT_EQ(_cumulativeMetrics.getObservedMetricsCount(), 2);
    ASSERT_EQ(_cumulativeMetrics.getOldestOperationRemainingTimeMillis(), kOldestTime);
}

TEST_F(ShardingDataTransformCumulativeMetricsTest, StillReportsOldestAfterRandomOperations) {
    doRandomOperationsTest<ScopedObserverMock>();
}

TEST_F(ShardingDataTransformCumulativeMetricsTest,
       StillReportsOldestAfterRandomOperationsMultithreaded) {
    doRandomOperationsMultithreadedTest<ScopedObserverMock>();
}

}  // namespace
}  // namespace mongo
