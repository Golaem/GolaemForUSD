/***************************************************************************
*                                                                          *
*  Copyright (C) Golaem S.A.  All Rights Reserved.                         *
*                                                                          *
***************************************************************************/

#include "glmUSDLogger.h"
#include "glmUSD.h"

USD_INCLUDES_START

#include <pxr/pxr.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/errorMark.h>

USD_INCLUDES_END

namespace glm
{
    namespace usdplugin
    {
        using namespace PXR_INTERNAL_NS;

        struct GolaemErrorCodes
        {
            enum Value
            {
                GOLAEM_ERROR,
                GOLAEM_SDK_ERROR,
                END,
            };
        };
        

        TF_REGISTRY_FUNCTION(TfEnum)
        {
            TF_ADD_ENUM_NAME(GolaemErrorCodes::GOLAEM_ERROR, "[Golaem]");
            TF_ADD_ENUM_NAME(GolaemErrorCodes::GOLAEM_SDK_ERROR, "[GolaemSDK]");
        }

        //-----------------------------------------------------------------------------
        USDLogger::USDLogger()
        {
        }

        //-----------------------------------------------------------------------------
        USDLogger::~USDLogger()
        {
        }

        //-----------------------------------------------------------------------------
        void USDLogger::trace(glm::Log::Module module, glm::Log::Severity severity, const char* msg, const char*, int, const char*)
        {
            glm::GlmString glmMessage = msg;
            glmMessage += "\n";
            GolaemErrorCodes::Value errorCode = GolaemErrorCodes::END;
            if (module == Log::CROWD)
            {
                errorCode = GolaemErrorCodes::GOLAEM_ERROR;
            }
            else if (module == Log::SDK)
            {
                errorCode = GolaemErrorCodes::GOLAEM_SDK_ERROR;
            }
            switch (severity)
            {
            case glm::Log::LOG_ERROR:
            {
                TF_ERROR(errorCode, glmMessage.c_str());
            }
            break;
            case glm::Log::LOG_WARNING:
            {
                TF_WARN(errorCode, glmMessage.c_str());
            }
            break;
            case glm::Log::LOG_INFO:
            {
                TF_STATUS(errorCode, glmMessage.c_str());
            }
            break;
            case glm::Log::LOG_DEBUG:
            {
                TF_STATUS(errorCode, glmMessage.c_str());
            }
            break;
            default:
                break;
            }
        }
    } // namespace usdplugin
} // namespace glm
