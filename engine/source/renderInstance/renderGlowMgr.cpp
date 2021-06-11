//-----------------------------------------------------------------------------
// Torque Shader Engine
// Copyright (C) GarageGames.com, Inc.
//-----------------------------------------------------------------------------
#include "renderGlowMgr.h"
#include "materials/sceneData.h"
#include "sceneGraph/sceneGraph.h"
#include "materials/matInstance.h"
#include "../../game/shaders/shdrConsts.h"


//**************************************************************************
// RenderGlowMgr
//**************************************************************************

//-----------------------------------------------------------------------------
// setup scenegraph data
//-----------------------------------------------------------------------------
void RenderGlowMgr::setupSGData(RenderInst* ri, SceneGraphData& data)
{
    dMemset(&data, 0, sizeof(SceneGraphData));

    dMemcpy(&data.light, &ri->light, sizeof(ri->light));
    dMemcpy(&data.lightSecondary, &ri->lightSecondary, sizeof(ri->lightSecondary));
    data.dynamicLight = ri->dynamicLight;
    data.dynamicLightSecondary = ri->dynamicLightSecondary;

    data.dynamicLight = ri->dynamicLight;
    //data.lightingTransform = ri->lightingTransform;

    data.camPos = gRenderInstManager.getCamPos();
    data.objTrans = *ri->objXform;
    data.backBuffTex = *ri->backBuffTex;
    data.cubemap = ri->cubemap;

    data.useFog = true;
    data.fogTex = getCurrentClientSceneGraph()->getFogTexture();
    data.fogHeightOffset = getCurrentClientSceneGraph()->getFogHeightOffset();
    data.fogInvHeightRange = getCurrentClientSceneGraph()->getFogInvHeightRange();
    data.visDist = getCurrentClientSceneGraph()->getVisibleDistanceMod();

    data.glowPass = true;

    if (ri->lightmap)
    {
        data.lightmap = *ri->lightmap;
    }
    if (ri->normLightmap)
    {
        data.normLightmap = *ri->normLightmap;
    }

    data.visibility = ri->visibility;

}


//-----------------------------------------------------------------------------
// render
//-----------------------------------------------------------------------------
void RenderGlowMgr::render()
{
#ifdef _XBOX
    return; // HACK -patw
#endif

   // Early out if nothing to draw.
    if (mElementList.empty())
        return;

    // CodeReview - This is pretty hackish. - AlexS 4/19/07
    if (GFX->getPixelShaderVersion() < 0.001f)
        return;

    GlowBuffer* glowBuffer = getCurrentClientSceneGraph()->getGlowBuff();
    if (!glowBuffer || glowBuffer->isDisabled() || mElementList.empty()) return;

    PROFILE_START(RenderGlowMgrRender);

    RectI vp = GFX->getViewport();

    GFX->pushActiveRenderSurfaces();
    glowBuffer->setAsRenderTarget();

    GFX->pushWorldMatrix();

    // set render states
    GFX->setCullMode(GFXCullCCW);
    GFX->setZWriteEnable(false);

    for (U32 i = 0; i < TEXTURE_STAGE_COUNT; i++)
    {
        GFX->setTextureStageAddressModeU(i, GFXAddressWrap);
        GFX->setTextureStageAddressModeV(i, GFXAddressWrap);

        GFX->setTextureStageMagFilter(i, GFXTextureFilterLinear);
        GFX->setTextureStageMinFilter(i, GFXTextureFilterLinear);
        GFX->setTextureStageMipFilter(i, GFXTextureFilterLinear);
    }

    // init loop data
    GFXVertexBuffer* lastVB = NULL;
    GFXPrimitiveBuffer* lastPB = NULL;
    SceneGraphData sgData;
    U32 binSize = mElementList.size();

    for (U32 j = 0; j < binSize; )
    {
        RenderInst* ri = mElementList[j].inst;

        // temp fix - these shouldn't submit glow ri's...
        if (ri->dynamicLight)
        {
            j++;
            continue;
        }

        setupSGData(ri, sgData);
        MatInstance* mat = ri->matInst;
        if (!mat)
        {
            mat = gRenderInstManager.getWarningMat();
        }
        U32 matListEnd = j;

        while (mat->setupPass(sgData))
        {
            U32 a;
            for (a = j; a < binSize; a++)
            {
                RenderInst* passRI = mElementList[a].inst;

                if (mat != passRI->matInst)  // new material, need to set up Material data
                {
                    break;
                }


                // fill in shader constants that change per draw
                //-----------------------------------------------
                GFX->setVertexShaderConstF(0, (float*)passRI->worldXform, 4);

                // set object transform
                MatrixF objTrans = *passRI->objXform;
                objTrans.transpose();
                GFX->setVertexShaderConstF(VC_OBJ_TRANS, (float*)&objTrans, 4);
                objTrans.transpose();
                objTrans.inverse();

                // fill in eye data
                Point3F eyePos = gRenderInstManager.getCamPos();
                objTrans.mulP(eyePos);
                GFX->setVertexShaderConstF(VC_EYE_POS, (float*)&eyePos, 1);

                // fill in light data
                Point3F lightDir = passRI->light.mDirection;
                objTrans.mulV( lightDir );
                GFX->setVertexShaderConstF( VC_LIGHT_DIR1, (float*)&lightDir, 1 );

                // fill in cubemap data
                if (mat->hasCubemap())
                {
                    Point3F cubeEyePos = gRenderInstManager.getCamPos() - passRI->objXform->getPosition();
                    GFX->setVertexShaderConstF(VC_CUBE_EYE_POS, (float*)&cubeEyePos, 1);

                    MatrixF cubeTrans = *passRI->objXform;
                    cubeTrans.setPosition(Point3F(0.0, 0.0, 0.0));
                    cubeTrans.transpose();
                    GFX->setVertexShaderConstF(VC_CUBE_TRANS, (float*)&cubeTrans, 3);
                }

                // set buffers if changed
                if (passRI->primBuff)
                {
                    if (lastVB != passRI->vertBuff->getPointer())
                    {
                        GFX->setVertexBuffer(*passRI->vertBuff);
                        lastVB = passRI->vertBuff->getPointer();
                    }
                    if (lastPB != passRI->primBuff->getPointer())
                    {
                        GFX->setPrimitiveBuffer(*passRI->primBuff);
                        lastPB = passRI->primBuff->getPointer();
                    }

                    // draw it
                    GFX->drawPrimitive(passRI->primBuffIndex);
                }
            }
            matListEnd = a;

        }

        // force increment if none happened, otherwise go to end of batch
        j = (j == matListEnd) ? j + 1 : matListEnd;

    }

    // restore render states, copy to screen
    GFX->setZWriteEnable(true);
    GFX->popActiveRenderSurfaces();
    glowBuffer->copyToScreen(vp);

    GFX->popWorldMatrix();

    PROFILE_END();
}