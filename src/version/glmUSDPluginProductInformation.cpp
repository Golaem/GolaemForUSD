/***************************************************************************
*                                                                          *
*  Copyright (C) Golaem S.A.  All Rights Reserved.                         *
*                                                                          *
***************************************************************************/

#include "glmUSDPluginProductInformation.h"
#include "glmUSDPluginVersion.h"

#include <glmProductInformation.h>

namespace glm
{
    namespace usdplugin
    {
        //-------------------------------------------------------------------------
        const ProductInformation& getProductInformation()
        {
            static ProductInformation glmProductInformation(
                GLM_USDPLUGIN_NAME,
                GLM_USDPLUGIN_DESCRIPTION,
                GLM_USDPLUGIN_MAJORVERSION,
                GLM_USDPLUGIN_MINORVERSION,
                GLM_USDPLUGIN_PATCHVERSION,
                GLM_USDPLUGIN_BRANCHVERSION,
                GLM_USDPLUGIN_RELEASE_LABEL,
                GLM_USDPLUGIN_RELEASE_DATE);
            return glmProductInformation;
        }
    } // namespace usdplugin
} // namespace glm
