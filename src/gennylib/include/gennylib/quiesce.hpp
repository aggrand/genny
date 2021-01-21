// Copyright 2021-present MongoDB Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HEADER_5936639A_7D22_4629_8FE1_4A08443DDB0F_INCLUDED
#define HEADER_5936639A_7D22_4629_8FE1_4A08443DDB0F_INCLUDED

#include <string_view>

#include <mongocxx/uri.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/database.hpp>
#include <bsoncxx/types.hpp>
#include <bsoncxx/array/view.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>

#include <boost/log/trivial.hpp>

#include <gennylib/topology.hpp>

namespace genny {

bool waitOplog(Topology& topology) {
    class WaitOplogVisitor : public TopologyVisitor {
    public:
        void visitReplSetDescriptionPre(const ReplSetDescription& desc) {
            using bsoncxx::builder::basic::kvp;
            using bsoncxx::builder::basic::make_document;
            mongocxx::client client(mongocxx::uri(desc.primaryUri));
            auto admin = client["admin"];

            // Assert that all nodes are in reasonable states.
            auto replSetRes = admin.run_command(make_document(kvp("replSetGetStatus", 1)));
            auto members = replSetRes.view()["members"];
            if (members && members.type() == bsoncxx::type::k_array) {
                bsoncxx::array::view members_view = members.get_array();
                for (auto member : members_view) {
                    std::string state(member["stateStr"].get_utf8().value);
                    if (state != "PRIMARY" && state != "SECONDARY" && state != "ARBITER") {
                        std::string name(member["name"].get_utf8().value);
                        BOOST_LOG_TRIVIAL(error) << "Cannot wait oplog, replset member "
                            << name << " is " << state;
                        success_acc = false;
                        return;
                    }
                }
            }

            // Do flush
            auto collection = admin["wait_oplog"];
            mongocxx::write_concern wc;
            wc.nodes(desc.nodes.size());
            wc.journal(true);
            mongocxx::options::insert opts;
            opts.write_concern(wc);
            auto insertRes = collection.insert_one(make_document(kvp("x", "flush")), opts);
        
            success_acc = success_acc && insertRes && insertRes->result().inserted_count() == 1;
        }
        bool success_acc = true;
    };

    WaitOplogVisitor waitVisitor;
    topology.accept(waitVisitor);

    return waitVisitor.success_acc;
}

void doFsync(Topology& topology) {
    class DoFsyncVisitor : public TopologyVisitor {
        void visitMongodDescription(const MongodDescription& desc) {
            using bsoncxx::builder::basic::kvp;
            using bsoncxx::builder::basic::make_document;

            mongocxx::client client(mongocxx::uri(desc.mongodUri));
            auto admin = client["admin"];
            admin.run_command(make_document(kvp("fsync", 1)));
        }
    };
    DoFsyncVisitor v;
    topology.accept(v);
}

/*
 * Helper function to quiesce the system and reduce noise.
 * The appropriate actions will be taken whether the target
 * is a standalone, replica set, or sharded cluster.
 */
bool quiesceImpl(mongocxx::pool::entry& client) {
    Topology topology(*client);
    waitOplog(topology);
    return true;
}

}

#endif  // HEADER_058638D3_7069_42DC_809F_5DB533FCFBA3_INCLUDED

