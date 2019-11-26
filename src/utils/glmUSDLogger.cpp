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

        enum GolaemErrorCodes
        {
            GLM_ERROR_CODE,
        };

        TF_REGISTRY_FUNCTION(TfEnum)
        {
            TF_ADD_ENUM_NAME(GLM_ERROR_CODE);
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
            std::string info = "";
            if (module == Log::CROWD)
            {
                info = "Golaem";
            }
            else if (module == Log::SDK)
            {
                info = "GolaemSDK";
            }
            switch (severity)
            {
            case glm::Log::LOG_ERROR:
            {
                TF_ERROR(info, GLM_ERROR_CODE, msg);
            }
            break;
            case glm::Log::LOG_WARNING:
            {
                TF_WARN(info, GLM_ERROR_CODE, msg);
            }
            break;
            case glm::Log::LOG_INFO:
            {
                TF_STATUS(info, GLM_ERROR_CODE, msg);
            }
            break;
            case glm::Log::LOG_DEBUG:
            {
                TF_STATUS(info, GLM_ERROR_CODE, msg);
            }
            break;
            default:
                break;
            }
        }
    } // namespace usdplugin
} // namespace glm
