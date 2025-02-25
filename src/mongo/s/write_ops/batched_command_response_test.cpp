/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include "mongo/platform/basic.h"

#include <memory>
#include <string>

#include "mongo/db/jsobj.h"
#include "mongo/db/ops/write_ops.h"
#include "mongo/s/stale_exception.h"
#include "mongo/s/write_ops/batched_command_response.h"
#include "mongo/s/write_ops/write_error_detail.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

TEST(BatchedCommandResponse, Basic) {
    BSONArray writeErrorsArray = BSON_ARRAY(
        BSON(WriteErrorDetail::index(0) << WriteErrorDetail::errCode(ErrorCodes::IndexNotFound)
                                        << WriteErrorDetail::errCodeName("IndexNotFound")
                                        << WriteErrorDetail::errMessage("index 0 failed")
                                        << WriteErrorDetail::errInfo(BSON("more info" << 1)))
        << BSON(WriteErrorDetail::index(1)
                << WriteErrorDetail::errCode(ErrorCodes::InvalidNamespace)
                << WriteErrorDetail::errCodeName("InvalidNamespace")
                << WriteErrorDetail::errMessage("index 1 failed too")
                << WriteErrorDetail::errInfo(BSON("more info" << 1))));

    BSONObj writeConcernError(
        BSON("code" << 8 << "codeName" << ErrorCodes::errorString(ErrorCodes::Error(8)) << "errmsg"
                    << "norepl"
                    << "errInfo" << BSON("a" << 1)));

    BSONObj origResponseObj =
        BSON(BatchedCommandResponse::n(0)
             << "opTime" << mongo::Timestamp(1ULL) << BatchedCommandResponse::writeErrors()
             << writeErrorsArray << BatchedCommandResponse::writeConcernError() << writeConcernError
             << "ok" << 1.0);

    std::string errMsg;
    BatchedCommandResponse response;
    bool ok = response.parseBSON(origResponseObj, &errMsg);
    ASSERT_TRUE(ok);

    BSONObj genResponseObj = BSONObjBuilder(response.toBSON()).append("ok", 1.0).obj();

    ASSERT_EQUALS(0, genResponseObj.woCompare(origResponseObj))
        << "\nparsed:   " << genResponseObj  //
        << "\noriginal: " << origResponseObj;
}

TEST(BatchedCommandResponse, TooManySmallErrors) {
    BatchedCommandResponse response;

    const auto bigstr = std::string(1024, 'x');

    for (int i = 0; i < 100'000; i++) {
        auto errDetail = std::make_unique<WriteErrorDetail>();
        errDetail->setIndex(i);
        errDetail->setStatus({ErrorCodes::BadValue, bigstr});
        response.addToErrDetails(errDetail.release());
    }

    response.setStatus(Status::OK());
    const auto bson = response.toBSON();
    ASSERT_LT(bson.objsize(), BSONObjMaxUserSize);
    const auto errDetails = bson["writeErrors"].Array();
    ASSERT_EQ(errDetails.size(), 100'000u);

    for (int i = 0; i < 100'000; i++) {
        auto errDetail = errDetails[i].Obj();
        ASSERT_EQ(errDetail["index"].Int(), i);
        ASSERT_EQ(errDetail["code"].Int(), ErrorCodes::BadValue);

        if (i < 1024) {
            ASSERT_EQ(errDetail["errmsg"].String(), bigstr) << i;
        } else {
            ASSERT_EQ(errDetail["errmsg"].String(), ""_sd) << i;
        }
    }
}

TEST(BatchedCommandResponse, TooManyBigErrors) {
    BatchedCommandResponse response;

    const auto bigstr = std::string(2'000'000, 'x');
    const auto smallstr = std::string(10, 'x');

    for (int i = 0; i < 100'000; i++) {
        auto errDetail = std::make_unique<WriteErrorDetail>();
        errDetail->setIndex(i);
        errDetail->setStatus({ErrorCodes::BadValue,          //
                              i < 10 ? bigstr : smallstr});  // Don't waste too much RAM.
        response.addToErrDetails(errDetail.release());
    }

    response.setStatus(Status::OK());
    const auto bson = response.toBSON();
    ASSERT_LT(bson.objsize(), BSONObjMaxUserSize);
    const auto errDetails = bson["writeErrors"].Array();
    ASSERT_EQ(errDetails.size(), 100'000u);

    for (int i = 0; i < 100'000; i++) {
        auto errDetail = errDetails[i].Obj();
        ASSERT_EQ(errDetail["index"].Int(), i);
        ASSERT_EQ(errDetail["code"].Int(), ErrorCodes::BadValue);

        if (i < 2) {
            ASSERT_EQ(errDetail["errmsg"].String(), bigstr) << i;
        } else {
            ASSERT_EQ(errDetail["errmsg"].String(), ""_sd) << i;
        }
    }
}

TEST(BatchedCommandResponse, NoDuplicateErrInfo) {
    auto verifySingleErrInfo = [](const BSONObj& obj) {
        size_t errInfo = 0;
        for (auto elem : obj) {
            if (elem.fieldNameStringData() == WriteErrorDetail::errInfo()) {
                ++errInfo;
            }
        }
        ASSERT_EQ(errInfo, 1) << "serialized obj with duplicate errInfo " << obj.toString();
    };

    // Construct a WriteErrorDetail.
    Status s(ErrorCodes::DocumentValidationFailure,
             "Document failed validation",
             BSON("errInfo" << BSON("detailed"
                                    << "error message")));
    BSONObjBuilder b;
    s.serialize(&b);
    WriteErrorDetail wed;
    wed.setIndex(0);

    // Verify it produces a single errInfo.
    wed.parseBSON(b.obj(), nullptr);
    BSONObj bsonWed = wed.toBSON();
    verifySingleErrInfo(bsonWed);

    BSONObjBuilder bcrBuilder;
    bcrBuilder.append("ok", 1);
    bcrBuilder.append("writeErrors", BSON_ARRAY(bsonWed));

    // Construct a 'BatchedCommandResponse' using the above 'bsonWed'.
    BatchedCommandResponse bcr;
    bcr.parseBSON(bcrBuilder.obj(), nullptr);
    BSONObj bsonBcr = bcr.toBSON();
    auto writeErrors = bsonBcr[BatchedCommandResponse::writeErrors()];
    ASSERT(!writeErrors.eoo());
    ASSERT_EQ(writeErrors.type(), BSONType::Array);

    // Verify that the entry in the 'writeErrors' array produces one 'errInfo' field.
    for (auto&& elem : writeErrors.Array()) {
        ASSERT_EQ(elem.type(), BSONType::Object);
        verifySingleErrInfo(elem.embeddedObject());
    }
}

TEST(BatchedCommandResponse, CompatibilityFromWriteErrorToBatchCommandResponse) {
    ChunkVersion versionReceived(1, 0, OID::gen(), Timestamp(2, 0));

    write_ops::UpdateCommandReply reply;
    reply.getWriteCommandReplyBase().setN(1);
    reply.getWriteCommandReplyBase().setWriteErrors(std::vector<write_ops::WriteError>{
        write_ops::WriteError(1,
                              Status(StaleConfigInfo(NamespaceString("TestDB", "TestColl"),
                                                     versionReceived,
                                                     boost::none,
                                                     ShardId("TestShard")),
                                     "Test stale config")),
    });

    BatchedCommandResponse response;
    ASSERT_TRUE(response.parseBSON(reply.toBSON(), nullptr));
    ASSERT_EQ(1U, response.getErrDetails().size());
    ASSERT_EQ(ErrorCodes::StaleShardVersion, response.getErrDetailsAt(0)->toStatus().code());
    ASSERT_EQ("Test stale config", response.getErrDetailsAt(0)->toStatus().reason());
    auto staleInfo =
        StaleConfigInfo::parseFromCommandError(response.getErrDetailsAt(0)->getErrInfo());
    ASSERT_EQ("TestDB.TestColl", staleInfo.getNss().ns());
    ASSERT_EQ(versionReceived, staleInfo.getVersionReceived());
    ASSERT(!staleInfo.getVersionWanted());
    ASSERT_EQ(ShardId("TestShard"), staleInfo.getShardId());
}

}  // namespace
}  // namespace mongo
