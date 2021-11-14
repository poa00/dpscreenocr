
#pragma once

#include <memory>

#include "backend/backend.h"


namespace dpso {
namespace backend {


using BackendCreatorFn = std::unique_ptr<Backend> (&)();
std::unique_ptr<Backend> createBackendExecutor(
    BackendCreatorFn creatorFn);


}
}