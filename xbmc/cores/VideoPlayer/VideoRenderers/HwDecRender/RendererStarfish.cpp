/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RendererStarfish.h"

#include "../RenderFactory.h"
#include "DVDCodecs/Video/DVDVideoCodecStarfish.h"
#include "ServiceBroker.h"
#include "rendering/gles/RenderSystemGLES.h"
#include "windowing/wayland/WinSystemWaylandWebOS.h"
#include "settings/MediaSettings.h"
#include "utils/log.h"

CRendererStarfish::CRendererStarfish()
{
  CLog::Log(LOGINFO, "Instancing CRendererStarfish");
}

CRendererStarfish::~CRendererStarfish()
{
  if (m_acbID) {
    long tid = 0;
    AcbAPI_setState(m_acbID, APPSTATE_FOREGROUND, PLAYSTATE_UNLOADED, &tid);
    AcbAPI_finalize(m_acbID);
    AcbAPI_destroy(m_acbID);
  }
}

CBaseRenderer* CRendererStarfish::Create(CVideoBuffer *buffer)
{
  if (buffer && dynamic_cast<CStarfishVideoBuffer*>(buffer))
    return new CRendererStarfish();
  return nullptr;
}

bool CRendererStarfish::Configure(const VideoPicture& picture, float fps, unsigned int orientation)
{
  auto windowName = dynamic_cast<KODI::WINDOWING::WAYLAND::CWinSystemWaylandWebOS*>(CServiceBroker::GetWinSystem())
        ->GetExportedWindowName();

  if (!windowName.size()) {
    auto mediaID = dynamic_cast<CStarfishVideoBuffer*>(picture.videoBuffer)->mediaID;
    CLog::Log(LOGDEBUG, "CRendererStarfish::Configure() for ACB, mediaID: {}", mediaID);
    m_acbID = AcbAPI_create();
    if (!AcbAPI_initialize(m_acbID, PLAYER_TYPE_MSE, getenv("APPID"), &CRendererStarfish::AcbCallback)) {
      CLog::Log(LOGERROR, "CRendererStarfish::Configure() ACB init failed");
      m_acbID = 0;
    } else {
      long tid;
      AcbAPI_setSinkType(m_acbID, SINK_TYPE_AUTO);
      AcbAPI_setMediaId(m_acbID, mediaID.c_str());
      AcbAPI_setState(m_acbID, APPSTATE_FOREGROUND, PLAYSTATE_LOADED, &tid);
      AcbAPI_setState(m_acbID, APPSTATE_FOREGROUND, PLAYSTATE_SEAMLESS_LOADED, &tid);
    }
  }

  m_iFlags = GetFlagsChromaPosition(picture.chroma_position) |
             GetFlagsColorMatrix(picture.color_space, picture.iWidth, picture.iHeight) |
             GetFlagsColorPrimaries(picture.color_primaries) |
             GetFlagsStereoMode(picture.stereoMode);

  return CLinuxRendererGLES::Configure(picture, fps, orientation);
}

bool CRendererStarfish::Register()
{
  VIDEOPLAYER::CRendererFactory::RegisterRenderer("starfish", CRendererStarfish::Create);
  return true;
}

void CRendererStarfish::ManageRenderArea()
{
  CBaseRenderer::ManageRenderArea();

  if((m_exportedDestRect != m_destRect || m_exportedSourceRect != m_sourceRect) && !m_sourceRect.IsEmpty() && !m_destRect.IsEmpty()) {
    CLog::Log(LOGDEBUG, "CRendererStarfish::ManageRenderArea()");
    auto origRect = CRect { 0, 0, static_cast<float>(m_sourceWidth), static_cast<float>(m_sourceHeight) };
    if (m_acbID != 0) {
      long tid;
      CLog::Log(LOGDEBUG, "CRendererStarfish ACB display: {} {} {}x{} ; {} {} {}x{}", m_sourceRect.x1, m_sourceRect.y1, m_sourceRect.Width(), m_sourceRect.Height(), m_destRect.x1, m_destRect.y1, m_destRect.Width(), m_destRect.Height());
      // AcbAPI_setDisplayWindow(m_acbID, m_destRect.x1, m_destRect.y1, m_destRect.Width(), m_destRect.Height(), true, &tid);
      AcbAPI_setCustomDisplayWindow(m_acbID, m_sourceRect.x1, m_sourceRect.y1, m_sourceRect.Width(), m_sourceRect.Height(), m_destRect.x1, m_destRect.y1, m_destRect.Width(), m_destRect.Height(), false, &tid);
    } else {
      dynamic_cast<KODI::WINDOWING::WAYLAND::CWinSystemWaylandWebOS*>(CServiceBroker::GetWinSystem())
          ->SetExportedWindow(origRect, m_sourceRect, m_destRect);
    }
    m_exportedSourceRect = m_sourceRect;
    m_exportedDestRect = m_destRect;
  }
}

bool CRendererStarfish::Supports(ERENDERFEATURE feature) const
{
  if (feature == RENDERFEATURE_ZOOM || feature == RENDERFEATURE_STRETCH ||
      feature == RENDERFEATURE_PIXEL_RATIO || feature == RENDERFEATURE_VERTICAL_SHIFT ||
      feature == RENDERFEATURE_ROTATION)
    return true;

  return false;
}

void CRendererStarfish::AddVideoPicture(const VideoPicture &picture, int index)
{
  if (!m_gotFirstFrame) {
    if (m_acbID != 0) {
      long tid;
      auto mediaVideoData = dynamic_cast<CStarfishVideoBuffer*>(picture.videoBuffer)->mediaVideoData;
      AcbAPI_setMediaVideoData(m_acbID, mediaVideoData.c_str());
    }
    m_gotFirstFrame = true;
  }
}

void CRendererStarfish::ReleaseBuffer(int idx)
{
}

CRenderInfo CRendererStarfish::GetRenderInfo()
{
  CRenderInfo info;
  info.max_buffer_size = 4;
  info.optimal_buffer_size = 3;
  return info;
}

bool CRendererStarfish::LoadShadersHook()
{
  return true;
}

bool CRendererStarfish::RenderHook(int index)
{
  return true;
}

bool CRendererStarfish::CreateTexture(int index)
{
  return true;
}

void CRendererStarfish::DeleteTexture(int index)
{
}

bool CRendererStarfish::UploadTexture(int index)
{
  return true;
}

bool CRendererStarfish::IsGuiLayer()
{
  return false;
}

void CRendererStarfish::RenderUpdateVideo(bool clear, unsigned int flags, unsigned int alpha)
{
}

void CRendererStarfish::AcbCallback(long acbId, long taskId, long eventType, long appState, long playState, const char *reply) {
  std::string logstr = "";
  if (reply != nullptr) {
    logstr = std::string(reply);
  }
  CLog::Log(LOGDEBUG, "CRendererStarfish::AcbCallback -> acbid: {}, taskid: {}, eventType: {}, appState: {}, playState: {}, reply: {}", acbId, taskId, eventType, appState, playState, logstr);
}
