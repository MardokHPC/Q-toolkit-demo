#pragma once

#include "report.hpp"

#include <string>

namespace qtoolkit {

// poolkit_advise has no API: the binary is the only surface. It writes
// its --json, and this reads back the fields the report carries, refusing a
// document that does not match the schema it claims. The reference configuration
// is the one the workload actually ran with: a recommendation computed against
// another table would be advice about a different pool.
[[nodiscard]] bool run_advisor(const std::string& trace_path, const std::string& advise_path,
                               const PoolConfigRecord& reference, AdviseBlock& out,
                               std::string& error);

}  // namespace qtoolkit
