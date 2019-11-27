/***************************************************************************
*                                                                          *
*  Copyright (C) Golaem S.A.  All Rights Reserved.                         *
*                                                                          *
***************************************************************************/

#include "glmUSD.h"
#include "glmUSDLogger.h"
#include "glmUSDPluginProductInformation.h"

#include <glmCrowdIO.h>
#include <glmMutex.h>
#include <glmScopedLock.h>
#include <glmProductInformation.h>

namespace glm
{
    namespace usdplugin
    {
        static glm::Mutex s_initLock;
        static int s_initCount = 0;

        //-----------------------------------------------------------------------------
        void init()
        {
            glm::ScopedLock<glm::Mutex> lock(s_initLock);
            if (s_initCount == 0)
            {
                printf("%s\n", usdplugin::getProductInformation().getCString());
                glm::crowdio::init();
                glm::Singleton<USDLogger>::create();
            }
            ++s_initCount;
        }

        //-------------------------------------------------------------------------
        void finish()
        {
            glm::ScopedLock<glm::Mutex> lock(s_initLock);
            --s_initCount;
            if (s_initCount == 0)
            {
                glm::Singleton<USDLogger>::destroy();
                glm::crowdio::finish();
            }
        }
    } // namespace usdplugin
} // namespace glm