﻿#include "stdafx.h"
#include "MapInstance.h"
#include "mapentity.h"
#include "mapdoc.h"
#include "mapworld.h"
#include "mapsolid.h"
#include "mapgroup.h"
#include "render2d.h"
#include "render3dms.h"
#include "toolinterface.h"
#include "mapview2d.h"
#include "camera.h"

#include "smartptr.h"
#include "fmtstr.h"
#include "chunkfile.h"
#include "KeyValues.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "tier2/renderutils.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

IMPLEMENT_MAPCLASS( CMapInstance );

CMapClass* CMapInstance::Create( CHelperInfo* pHelperInfo, CMapEntity* pParent )
{
	return new CMapInstance( pParent );
}

CMapInstance::CMapInstance() : m_pTemplate( nullptr )
{
	m_matTransform.Identity();
}

CMapInstance::CMapInstance( CMapEntity* pParent ) : m_pTemplate( nullptr )
{
	m_matTransform.Identity();
	QAngle angles;
	Vector origin;
	pParent->GetAngles( angles );
	pParent->GetOrigin( origin );
	m_matTransform.SetupMatrixOrgAngles( origin, angles );
	m_strCurrentVMF = pParent->GetKeyValue( "file" );
	LoadVMF( pParent );
}

CMapInstance::~CMapInstance()
{
	delete m_pTemplate;
}

CMapClass* CMapInstance::Copy( bool bUpdateDependencies )
{
	CMapInstance* inst = new CMapInstance;
	if ( inst != nullptr )
		inst->CopyFrom( this, bUpdateDependencies );
	return inst;
}

CMapClass* CMapInstance::CopyFrom( CMapClass* pFrom, bool bUpdateDependencies )
{
	Assert( pFrom->IsMapClass( MAPCLASS_TYPE( CMapInstance ) ) );
	CMapInstance* pObject = static_cast<CMapInstance*>( pFrom );

	CMapHelper::CopyFrom( pObject, bUpdateDependencies );

	m_strCurrentVMF = pObject->m_strCurrentVMF;
	m_strPreviousVMF = pObject->m_strPreviousVMF;
	m_matTransform = pObject->m_matTransform;

	return this;
}

void CMapInstance::UpdateDependencies( CMapWorld* pWorld, CMapClass* pObject )
{
	if ( m_strCurrentVMF != m_strPreviousVMF )
		LoadVMF();
}

void CMapInstance::SetParent( CMapAtom* pParent )
{
	CMapHelper::SetParent( pParent );
	if ( m_pTemplate )
		m_pTemplate->SetPreferredPickObject( GetParent() );
}

SelectionState_t CMapInstance::SetSelectionState( SelectionState_t eSelectionState )
{
	const SelectionState_t old = CMapHelper::SetSelectionState( eSelectionState );
	//if ( m_pTemplate )
	//	m_pTemplate->SetSelectionState( eSelectionState == SELECT_NONE ? SELECT_NONE : SELECT_NORMAL );
	return old;
}

void CMapInstance::SetOrigin( Vector& pfOrigin )
{
	CMapHelper::SetOrigin( pfOrigin );
}

void CMapInstance::SetCullBoxFromFaceList( CMapFaceList* pFaces )
{
	if ( m_pTemplate )
		m_pTemplate->SetCullBoxFromFaceList( pFaces );
	else
		CMapHelper::SetCullBoxFromFaceList( pFaces );
}

void CMapInstance::CalcBounds( BOOL bFullUpdate )
{
	if ( m_pTemplate )
		m_pTemplate->CalcBounds( bFullUpdate );

	m_Render2DBox.bmins = m_CullBox.bmins = m_Origin - Vector( 8 );
	m_Render2DBox.bmaxs = m_CullBox.bmaxs = m_Origin + Vector( 8 );
}

void CMapInstance::GetCullBox( Vector& mins, Vector& maxs )
{
	if ( m_pTemplate )
		GetBounds<&CMapClass::m_CullBox>( mins, maxs );
	else
		CMapHelper::GetCullBox( mins, maxs );
}

bool CMapInstance::GetCullBoxChild( Vector& mins, Vector& maxs )
{
	GetCullBox( mins, maxs );
	return true;
}

void CMapInstance::GetRender2DBox( Vector& mins, Vector& maxs )
{
	if ( m_pTemplate )
		GetBounds<&CMapClass::m_Render2DBox>( mins, maxs );
	else
		CMapHelper::GetRender2DBox( mins, maxs );
}

bool CMapInstance::GetRender2DBoxChild( Vector& mins, Vector& maxs )
{
	GetRender2DBox( mins, maxs );
	return true;
}

void CMapInstance::GetBoundsCenter( Vector& vecCenter )
{
	if ( m_pTemplate )
	{
		Vector mins, maxs;
		GetBounds<&CMapClass::m_Render2DBox>(mins, maxs );
		VectorLerp( mins, maxs, 0.5f, vecCenter );
	}
	else
		CMapHelper::GetBoundsCenter( vecCenter );
}

bool CMapInstance::GetBoundsCenterChild( Vector & vecCenter )
{
	GetBoundsCenter( vecCenter );
	return true;
}

void CMapInstance::GetBoundsSize( Vector& vecSize )
{
	if ( m_pTemplate )
	{
		Vector mins, maxs;
		GetBounds<&CMapClass::m_Render2DBox>( mins, maxs );
		VectorSubtract( maxs, mins, vecSize );
	}
	else
		CMapHelper::GetBoundsSize( vecSize );
}

bool CMapInstance::GetBoundsSizeChild( Vector & vecSize )
{
	GetBoundsSize( vecSize );
	return true;
}

#pragma float_control(precise, on, push)
void CMapInstance::DoTransform( const VMatrix& matrix )
{
	CMapHelper::DoTransform( matrix );

	m_matTransform = matrix * m_matTransform;
	QAngle angle;
	Vector origin;
	DecompressMatrix( origin, angle );
	FixAngles( angle );
	ConstructMatrix( origin, angle );

	while ( angle[YAW] < 0 )
	{
		angle[YAW] += 360;
	}

	if ( CMapEntity* pEntity = dynamic_cast<CMapEntity*>( m_pParent ) )
	{
		char szValue[80];
		sprintf( szValue, "%g %g %g", angle[0], angle[1], angle[2] );
		pEntity->NotifyChildKeyChanged( this, "angles", szValue );
	}
}
#pragma float_control(pop)

bool CMapInstance::PostloadVisGroups( bool bIsLoading )
{
	if ( m_pTemplate )
	{
		m_pTemplate->PostloadVisGroups();
	}

	return CMapHelper::PostloadVisGroups( bIsLoading );
}

bool CMapInstance::HitTest2D( CMapView2D* pView, const Vector2D& point, HitInfo_t& HitData )
{
	Vector world;
	pView->ClientToWorld( world, point );
	Vector2D transformed;
	pView->WorldToClient( transformed, m_matTransform.InverseTR().VMul4x3( world ) );
	if ( m_pTemplate && m_pTemplate->HitTest2D( pView, transformed, HitData ) )
		return true;
	return CMapHelper::HitTest2D( pView, point, HitData );
}

bool CMapInstance::IsCulledByCordon( const Vector& vecMins, const Vector& vecMaxs )
{
	return !IsIntersectingBox( vecMins, vecMaxs );
}

bool CMapInstance::IsInsideBox( Vector const& pfMins, Vector const& pfMaxs ) const
{
	if ( m_pTemplate )
	{
		Vector bmins, bmaxs;
		GetBounds<&CMapClass::m_Render2DBox>( bmins, bmaxs );

		if ( bmins[0] < pfMins[0] || bmaxs[0] > pfMaxs[0] )
			return CMapHelper::IsInsideBox( pfMins, pfMaxs );

		if ( bmins[1] < pfMins[1] || bmaxs[1] > pfMaxs[1] )
			return CMapHelper::IsInsideBox( pfMins, pfMaxs );

		if ( bmins[2] < pfMins[2] || bmaxs[2] > pfMaxs[2] )
			return CMapHelper::IsInsideBox( pfMins, pfMaxs );

		return true;
	}
	else
		return CMapHelper::IsInsideBox( pfMins, pfMaxs );
}

bool CMapInstance::IsIntersectingBox( const Vector& vecMins, const Vector& vecMaxs ) const
{
	if ( m_pTemplate )
	{
		Vector bmins, bmaxs;
		GetBounds<&CMapClass::m_Render2DBox>( bmins, bmaxs );

		if ( bmins[0] >= vecMaxs[0] || bmaxs[0] <= vecMins[0] )
			return CMapHelper::IsIntersectingBox( vecMins, vecMaxs );

		if ( bmins[1] >= vecMaxs[1] || bmaxs[1] <= vecMins[1] )
			return CMapHelper::IsIntersectingBox( vecMins, vecMaxs );

		if ( bmins[2] >= vecMaxs[2] || bmaxs[2] <= vecMins[2] )
			return CMapHelper::IsIntersectingBox( vecMins, vecMaxs );

		return true;
	}
	else
		return CMapHelper::IsIntersectingBox( vecMins, vecMaxs );
}

void CMapInstance::OnParentKeyChanged( const char* key, const char* value )
{
	if ( !stricmp( key, "file" ) && ( !m_strCurrentVMF.IsEqual_CaseInsensitive( value ) || !m_pTemplate ) )
	{
		m_strPreviousVMF = m_strCurrentVMF;
		m_strCurrentVMF.Set( value );
		LoadVMF();
		PostUpdate( Notify_Changed );
	}
	else if ( !stricmp( key, "angles" ) )
	{
		QAngle angle;
		Vector origin;
		DecompressMatrix( origin, angle );
		sscanf_s( value, "%f %f %f", &angle.x, &angle.y, &angle.z );
		ConstructMatrix( origin, angle );
		PostUpdate( Notify_Changed );
	}
	else if ( !stricmp( key, "origin" ) )
	{
		QAngle angle;
		Vector origin;
		DecompressMatrix( origin, angle );
		sscanf_s( value, "%f %f %f", &origin.x, &origin.y, &origin.z );
		ConstructMatrix( origin, angle );
		PostUpdate( Notify_Changed );
	}
}

void CMapInstance::Render2DChildren( CRender2D* pRender, CMapClass* pEnt )
{
	const CMapObjectList& children = pEnt->m_Children;
	for ( CMapClass* pChild : children )
	{
		if ( pChild && pChild->IsVisible() && pChild->IsVisible2D() )
		{
			if ( CBaseTool* toolObject = pChild->GetToolObject( 0, false ) )
			{
				if ( toolObject->GetToolID() != TOOL_SWEPT_HULL )
					continue;
			}

			pChild->Render2D( pRender );
			Render2DChildren( pRender, pChild );
		}
	}
}

void CMapInstance::Render2D( CRender2D* pRender )
{
	if ( !m_pTemplate )
		return;
	const ShowInstance_t visibility = GetWorldObject( this )->GetInstanceVisibility();
	if ( visibility == ShowInstance_t::INSTANCES_HIDE )
		return;

	VMatrix localTransform;
	const bool inLocalTransform = pRender->IsInLocalTransformMode();
	if ( !inLocalTransform )
		pRender->BeginLocalTransfrom( m_matTransform );
	else
	{
		VMatrix newLocalTransform;
		pRender->GetLocalTranform( localTransform );
		pRender->EndLocalTransfrom();
		ConcatTransforms( localTransform.As3x4(), m_matTransform.As3x4(), newLocalTransform.As3x4() );
		pRender->BeginLocalTransfrom( newLocalTransform );
	}

	Render2DChildren( pRender, m_pTemplate );

	if ( !inLocalTransform )
		pRender->EndLocalTransfrom();
	else
	{
		pRender->EndLocalTransfrom();
		pRender->BeginLocalTransfrom( localTransform );
	}

	pRender->PushRenderMode( RENDER_MODE_WIREFRAME );
	Vector mins, maxs;
	GetRender2DBox( mins, maxs );
	pRender->SetDrawColor( 255, 0, 255 );
	pRender->DrawBox( mins, maxs );
	pRender->PopRenderMode();
}

void CMapInstance::Render3DChildren( CRender3D* pRender, CUtlVector<CMapClass*>& deferred, CMapClass* pEnt, bool ignoreFrameCount )
{
	const EditorRenderMode_t renderMode = pRender->GetCurrentRenderMode();
	const CMapObjectList& children = pEnt->m_Children;
	for ( CMapClass* pChild : children )
	{
		if ( pChild && pChild->IsVisible() && ( ignoreFrameCount || pChild->GetRenderFrame() <= GetRenderFrame() ) )
		{
			pChild->SetRenderFrame( GetRenderFrame() + 1 );
			bool should_appear = true;
			if ( renderMode == RENDER_MODE_LIGHT_PREVIEW2 )
				should_appear &= pChild->ShouldAppearInLightingPreview();

			if ( renderMode == RENDER_MODE_LIGHT_PREVIEW_RAYTRACED )
				should_appear &= pChild->ShouldAppearInLightingPreview();

			if ( !should_appear )
				continue;

			if ( CBaseTool* toolObject = pChild->GetToolObject( 0, false ) )
			{
				if ( toolObject->GetToolID() != TOOL_SWEPT_HULL )
					continue;
			}

			if ( !pChild->ShouldRenderLast() )
				pChild->Render3D( pRender );
			else
			{
				deferred.AddToTail( pChild );
				continue;
			}
			Render3DChildren( pRender, deferred, pChild, ignoreFrameCount );
		}
	}
}

void CMapInstance::Render3DChildrenDeferred( CRender3D* pRender, CMapClass* pEnt, bool ignoreFrameCount )
{
	const EditorRenderMode_t renderMode = pRender->GetCurrentRenderMode();

	pEnt->Render3D( pRender );

	const CMapObjectList& children = pEnt->m_Children;
	for ( CMapClass* pChild : children )
	{
		if ( pChild && pChild->IsVisible() && ( ignoreFrameCount || pChild->GetRenderFrame() <= GetRenderFrame() ) )
		{
			pChild->SetRenderFrame( GetRenderFrame() + 1 );
			bool should_appear = true;
			if ( renderMode == RENDER_MODE_LIGHT_PREVIEW2 )
				should_appear &= pChild->ShouldAppearInLightingPreview();

			if ( renderMode == RENDER_MODE_LIGHT_PREVIEW_RAYTRACED )
				should_appear &= pChild->ShouldAppearInLightingPreview();

			if ( !should_appear )
				continue;

			Render3DChildrenDeferred( pRender, pChild, ignoreFrameCount );
		}
	}
}

void CMapInstance::Render3D( CRender3D* pRender )
{
	if ( m_pTemplate )
	{
		const ShowInstance_t visibility = GetWorldObject( this )->GetInstanceVisibility();
		if ( visibility == ShowInstance_t::INSTANCES_HIDE )
			return;

		CAutoPushPop guard2( pRender->m_DeferRendering, false );

		VMatrix localTransform;
		const bool inLocalTransform = pRender->IsInLocalTransformMode();
		if ( !inLocalTransform )
			pRender->BeginLocalTransfrom( m_matTransform );
		else
		{
			pRender->GetLocalTranform( localTransform );
			pRender->EndLocalTransfrom();
			VMatrix newLocalTransform;
			ConcatTransforms( localTransform.As3x4(), m_matTransform.As3x4(), newLocalTransform.As3x4() );
			pRender->BeginLocalTransfrom( newLocalTransform );
		}

		CUtlVector<CMapClass*> deferred;
		Render3DChildren( pRender, deferred, m_pTemplate, false );

		for ( CMapClass* def : deferred )
			Render3DChildrenDeferred( pRender, def, false );

		const EditorRenderMode_t mode = pRender->GetCurrentRenderMode();
		if ( ( visibility == ShowInstance_t::INSTANCES_SHOW_TINTED || GetSelectionState() != SELECT_NONE ) && ( mode == RENDER_MODE_FLAT || mode == RENDER_MODE_TEXTURED || mode == RENDER_MODE_TEXTURED_SHADED ) )
		{
			{
				int width, height;
				pRender->GetCamera()->GetViewPort( width, height );

				CMatRenderContextPtr pRenderContext( materials );
				pRenderContext->SetStencilEnable( true );
				//pRenderContext->ClearBuffers( false, false, true );
				pRenderContext->ClearStencilBufferRectangle( 0, 0, width, height, 1 );
				pRenderContext->OverrideColorWriteEnable( true, false );
				pRenderContext->SetStencilReferenceValue( GetID() );
				pRenderContext->SetStencilTestMask( 0xFFFF );
				pRenderContext->SetStencilWriteMask( 0xFFFF );
				pRenderContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_EQUAL );
				pRenderContext->SetStencilPassOperation( STENCILOPERATION_KEEP );
				pRenderContext->SetStencilFailOperation( STENCILOPERATION_REPLACE );
				pRenderContext->SetStencilZFailOperation( STENCILOPERATION_REPLACE );
			}

			deferred.RemoveAll();
			Render3DChildren( pRender, deferred, m_pTemplate, true );

			for ( CMapClass* def : deferred )
				Render3DChildrenDeferred( pRender, def, true );

			if ( !inLocalTransform )
				pRender->EndLocalTransfrom();
			else
			{
				pRender->EndLocalTransfrom();
				pRender->BeginLocalTransfrom( localTransform );
			}

			{
				int width, height;
				pRender->GetCamera()->GetViewPort( width, height );

				static CMaterialReference mat;
				if ( !mat.IsValid() )
				{
					KeyValues* kv = new KeyValues( "UnlitGeneric" );
					kv->SetString( "$basetexture", "white" );
					kv->SetString( "$color", "[0 0 0]" );
					kv->SetFloat( "$alpha", 0.75f );
					kv->SetBool( "$vertexcolor", true );
					mat.Init( "__instance_overlay", kv );
					mat->Refresh();
				}

				constexpr float redEmpty = 121 / 255.f;
				constexpr float redSelect = 175 / 255.f;
				constexpr float green = 114 / 255.f;
				constexpr float blue = 2 / 255.f;

				if ( GetSelectionState() == SELECT_NONE )
					mat->ColorModulate( redEmpty, green, blue );
				else
					mat->ColorModulate( redSelect, green, blue );
				CMatRenderContextPtr pRenderContext( materials );
				//pRenderContext->ClearBuffersObeyStencilEx( false, true, false );
				pRenderContext->OverrideColorWriteEnable( false, false );
				//pRenderContext->OverrideAlphaWriteEnable( true, true );
				pRenderContext->SetStencilWriteMask( 0 );
				//pRenderContext->SetStencilTestMask( 0xFFFFFFFF );
				//pRenderContext->SetStencilReferenceValue( GetID() );
				/*pRenderContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_EQUAL );
				pRenderContext->SetStencilPassOperation( STENCILOPERATION_KEEP );
				pRenderContext->SetStencilFailOperation( STENCILOPERATION_KEEP );
				pRenderContext->SetStencilZFailOperation( STENCILOPERATION_KEEP );*/
			//	pRenderContext->PerformFullScreenStencilOperation();

				DrawScreenSpaceRectangle(
					mat, 0, 0, width, height,
					0,0,
					width - 1, height -1,
					width, height);

				//pRenderContext->OverrideAlphaWriteEnable( false, false );
				pRenderContext->SetStencilEnable( false );
			}
		}
		else
		{
			if ( !inLocalTransform )
				pRender->EndLocalTransfrom();
			else
			{
				pRender->EndLocalTransfrom();
				pRender->BeginLocalTransfrom( localTransform );
			}
		}
	}
	pRender->RenderBox( m_Render2DBox.bmins, m_Render2DBox.bmaxs, 220, 220, 220, GetSelectionState() );
}

bool CMapInstance::RenderPreload( CRender3D* pRender, bool bNewContext )
{
	if ( m_pTemplate )
	{
		return m_pTemplate->RenderPreload( pRender, bNewContext );
	}
	else
		return CMapHelper::RenderPreload( pRender, bNewContext );
}

void CMapInstance::AddShadowingTriangles( CUtlVector<Vector>& tri_list )
{
	if ( m_pTemplate && GetWorldObject( this )->GetInstanceVisibility() != ShowInstance_t::INSTANCES_HIDE )
	{
		const int prevCount = tri_list.Count();
		AddShadowingTrianglesChildren( tri_list, m_pTemplate );
		// transform into correct space
		for ( int i = prevCount; i < tri_list.Count(); ++i )
		{
			Vector& vec = tri_list[i];
			vec = m_matTransform.VMul4x3( vec );
		}
	}
}

// Logic from VBSP
template <size_t N>
bool CMapInstance::DeterminePath( const char* pszBaseFileName, const char* pszInstanceFileName, char( &pszOutFileName )[N] )
{
	char szInstanceFileNameFixed[ MAX_PATH ];
	const char *pszMapPath = "\\maps\\";

	V_strcpy_safe( szInstanceFileNameFixed, pszInstanceFileName );
	V_SetExtension( szInstanceFileNameFixed, ".vmf", sizeof( szInstanceFileNameFixed ) );
	V_FixSlashes( szInstanceFileNameFixed );

	// first, try to find a relative location based upon the Base file name
	V_strcpy_safe( pszOutFileName, pszBaseFileName );
	V_StripFilename( pszOutFileName );

	V_strcat_safe( pszOutFileName, "\\" );
	V_strcat_safe( pszOutFileName, szInstanceFileNameFixed );

	if ( g_pFullFileSystem->FileExists( pszOutFileName ) )
		return true;

	// second, try to find the master 'maps' directory and make it relative from that
	V_strcpy_safe( pszOutFileName, pszBaseFileName );
	V_StripFilename( pszOutFileName );
	V_RemoveDotSlashes( pszOutFileName );
	V_FixDoubleSlashes( pszOutFileName );
	V_strlower( pszOutFileName );
	V_strcat_safe( pszOutFileName, "\\" );

	char* pos = strstr( pszOutFileName, pszMapPath );
	if ( pos )
	{
		pos += V_strlen( pszMapPath );
		*pos = 0;
		V_strcat_safe( pszOutFileName, szInstanceFileNameFixed );

		if ( g_pFullFileSystem->FileExists( pszOutFileName ) )
			return true;
	}

	if ( g_pGameConfig->m_szInstanceDir[0] )
	{
		V_sprintf_safe( szInstanceFileNameFixed, "%s%s", g_pGameConfig->m_szInstanceDir, pszInstanceFileName );

		if ( g_pFullFileSystem->FileExists( szInstanceFileNameFixed, "GAME" ) )
		{
			char FullPath[ MAX_PATH ];
			g_pFullFileSystem->RelativePathToFullPath( szInstanceFileNameFixed, "GAME", FullPath, sizeof( FullPath ) );
			V_strcpy_safe( pszOutFileName, FullPath );

			return true;
		}
	}

	pszOutFileName[0] = 0;
	return false;
}

void CMapInstance::LoadVMF( CMapClass* pParent )
{
	if ( m_pTemplate )
	{
		delete m_pTemplate;
		m_pTemplate = nullptr;
	}

	if ( !m_strCurrentVMF.IsEmpty() )
	{
		CMapWorld* world = GetWorldObject( pParent ? pParent : GetParent() );
		Assert( world );
		char instancePath[MAX_PATH];
		if ( DeterminePath( world->GetVMFPath(), m_strCurrentVMF, instancePath ) && LoadVMFInternal( instancePath ) )
			m_pTemplate->SetPreferredPickObject( pParent ? pParent : GetParent() );
	}
}

bool CMapInstance::LoadVMFInternal( const char* pVMFPath )
{
	if ( !m_pTemplate )
	{
		m_pTemplate = new CMapWorld;
		m_pTemplate->SetOwningDoc( nullptr );
	}
	m_pTemplate->SetVMFPath( pVMFPath );

	CChunkFile file;
	ChunkFileResult_t eResult = file.Open( pVMFPath, ChunkFile_Read );
	if ( eResult == ChunkFile_Ok )
	{
		ChunkFileResult_t( *LoadWorldCallback )( CChunkFile*, CMapInstance* ) = []( CChunkFile* pFile, CMapInstance* pDoc )
		{
			return pDoc->m_pTemplate->LoadVMF( pFile );
		};

		ChunkFileResult_t( *LoadEntityCallback )( CChunkFile*, CMapInstance* ) = []( CChunkFile* pFile, CMapInstance* pDoc )
		{
			CMapEntity* pEntity = new CMapEntity;
			pEntity->SetInstance( true );
			if ( pEntity->LoadVMF( pFile ) == ChunkFile_Ok )
				pDoc->m_pTemplate->AddChild( pEntity );
			else
				delete pEntity;
			return ChunkFile_Ok;
		};

		CChunkHandlerMap handlers;
		handlers.AddHandler( "world", LoadWorldCallback, this );
		handlers.AddHandler( "entity", LoadEntityCallback, this );
		handlers.SetErrorHandler( []( CChunkFile*, const char*, void* ) { return false; }, nullptr );

		file.PushHandlers( &handlers );

		while ( eResult == ChunkFile_Ok )
			eResult = file.ReadChunk();

		if ( eResult == ChunkFile_EOF )
			eResult = ChunkFile_Ok;

		file.PopHandlers();
	}

	if ( eResult == ChunkFile_Ok )
		m_pTemplate->PostloadWorld();
	else
	{
		delete m_pTemplate;
		m_pTemplate = nullptr;
	}

	return eResult == ChunkFile_Ok;
}

void CMapInstance::AddShadowingTrianglesChildren( CUtlVector<Vector>& tri_list, CMapClass* pEnt )
{
	CMapObjectList& children = pEnt->m_Children;
	for ( CMapClass* pChild : children )
	{
		if ( pChild && pChild->IsVisible() && pChild->ShouldAppearInLightingPreview() )
		{
			pChild->AddShadowingTriangles( tri_list );
			AddShadowingTrianglesChildren( tri_list, pChild );
		}
	}
}

#pragma float_control(precise, on, push)
template <BoundBox CMapClass::* type>
void CMapInstance::GetBounds( Vector& mins, Vector& maxs ) const
{
	if ( m_pTemplate )
	{
		( m_pTemplate->*type ).GetBounds( mins, maxs );
		Vector box[8];
		PointsFromBox( mins, maxs, box );
		for ( int i = 0; i < 8; ++i )
		{
			const Vector& transformed = m_matTransform.VMul4x3( box[i] );
			for ( int j = 0; j < 3; ++j )
			{
				if ( i == 0 || transformed[j] < mins[j] )
					mins[j] = transformed[j];
				if ( i == 0 || transformed[j] > maxs[j] )
					maxs[j] = transformed[j];
			}
		}
		return;
	}
	Assert( 0 );
}

void CMapInstance::FixAngles( QAngle& angle )
{
	for ( int i = 0; i < 3; ++i )
	{
		if ( fabs( angle[i] ) < 0.001 )
			angle[i] = 0;
		else
			angle[i] = static_cast<int>( static_cast<double>( angle[i] ) * 100 + ( fsign( angle[i] ) * 0.5 ) ) / 100;
	}
}

void CMapInstance::DecompressMatrix( Vector& origin, QAngle& angle ) const
{
	origin = m_matTransform.GetTranslation();
	MatrixToAngles( m_matTransform, angle );
}

void CMapInstance::ConstructMatrix( const Vector& origin, const QAngle& angle )
{
	m_matTransform.SetupMatrixOrgAngles( origin, angle );
}
#pragma float_control(pop)


// TODO: Move out these lambdas
bool CMapInstance::Collapse( bool bRecursive, InstanceCollapseData_t& collapseData )
{
	if ( m_strCurrentVMF.IsEmpty() )
		return false;
	CMapWorld* world = GetWorldObject( GetParent() );
	Assert( world );
	char instancePath[MAX_PATH];
	if ( !DeterminePath( world->GetVMFPath(), m_strCurrentVMF, instancePath ) )
		return false;

	const int chiCount = collapseData.newChildren.Count();
	const int visCount = collapseData.visGroups.Count();

	CChunkFile file;
	ChunkFileResult_t eResult = file.Open( instancePath, ChunkFile_Read );
	if ( eResult == ChunkFile_Ok )
	{
		ChunkFileResult_t( *LoadWorldCallback )( CChunkFile*, InstanceCollapseData_t* ) = []( CChunkFile* pFile, InstanceCollapseData_t* pDoc )->ChunkFileResult_t
		{
			ChunkFileResult_t( *LoadSolidCallback )( CChunkFile*, InstanceCollapseData_t* ) = []( CChunkFile* pFile, InstanceCollapseData_t* pDoc )->ChunkFileResult_t
			{
				CMapSolid* pSolid = new CMapSolid;

				bool bValid;
				const ChunkFileResult_t eResult = pSolid->LoadVMF( pFile, bValid );
				if ( eResult == ChunkFile_Ok && bValid )
				{
					const char* pszValue = pSolid->GetEditorKeyValue( "cordonsolid" );
					if ( pszValue == nullptr )
						pDoc->newChildren.AddToTail( pSolid );
				}
				else
					delete pSolid;

				return eResult;
			};

			struct SubLoadHiddenData
			{
				InstanceCollapseData_t& data;
				ChunkFileResult_t( *callback )( CChunkFile*, InstanceCollapseData_t* );
			};

			ChunkFileResult_t( *LoadHiddenCallback )( CChunkFile*, SubLoadHiddenData* ) = []( CChunkFile* pFile, SubLoadHiddenData* pDoc )->ChunkFileResult_t
			{
				CChunkHandlerMap handlers;
				handlers.AddHandler( "solid", pDoc->callback, &pDoc->data );

				pFile->PushHandlers( &handlers );
				const ChunkFileResult_t eResult = pFile->ReadChunk();
				pFile->PopHandlers();

				return eResult;
			};

			ChunkFileResult_t( *LoadGroupCallback )( CChunkFile*, InstanceCollapseData_t* ) = []( CChunkFile* pFile, InstanceCollapseData_t* pDoc )->ChunkFileResult_t
			{
				CMapGroup* pGroup = new CMapGroup;
				const ChunkFileResult_t eResult = pGroup->LoadVMF( pFile );
				if ( eResult == ChunkFile_Ok )
					pDoc->newChildren.AddToTail( pGroup );
				else
					delete pGroup;

				return eResult;
			};

			SubLoadHiddenData subLoadHidden { *pDoc, LoadSolidCallback };

			CChunkHandlerMap handlers;
			handlers.AddHandler( "solid", LoadSolidCallback, pDoc );
			handlers.AddHandler( "hidden", LoadHiddenCallback, &subLoadHidden );
			handlers.AddHandler( "group", LoadGroupCallback, pDoc );

			pFile->PushHandlers( &handlers );
			const ChunkFileResult_t eResult = pFile->ReadChunk();
			pFile->PopHandlers();

			return eResult;
		};

		ChunkFileResult_t( *LoadEntityCallback )( CChunkFile*, InstanceCollapseData_t* ) = []( CChunkFile* pFile, InstanceCollapseData_t* pDoc )->ChunkFileResult_t
		{
			CMapEntity* pEntity = new CMapEntity;
			if ( pEntity->LoadVMF( pFile ) == ChunkFile_Ok )
				pDoc->newChildren.AddToTail( pEntity );
			else
				delete pEntity;
			return ChunkFile_Ok;
		};

		struct SubLoadHiddenData
		{
			InstanceCollapseData_t& data;
			ChunkFileResult_t( *callback )( CChunkFile*, InstanceCollapseData_t* );
		};

		ChunkFileResult_t( *LoadHiddenCallback )( CChunkFile*, SubLoadHiddenData* ) = []( CChunkFile* pFile, SubLoadHiddenData* pDoc )->ChunkFileResult_t
		{
			CChunkHandlerMap handlers;
			handlers.AddHandler( "entity", pDoc->callback, &pDoc->data );

			pFile->PushHandlers( &handlers );
			const ChunkFileResult_t eResult = pFile->ReadChunk();
			pFile->PopHandlers();

			return eResult;
		};

		ChunkFileResult_t( *LoadVisGroupsCallback )( CChunkFile*, InstanceCollapseData_t* ) = []( CChunkFile* pFile, InstanceCollapseData_t* pDoc )->ChunkFileResult_t
		{
			// Fill out a little context blob for passing to the handler.
			struct SubLoadVisData
			{
				ChunkFileResult_t( *callback )( CChunkFile*, SubLoadVisData* );
				InstanceCollapseData_t& data;
				CVisGroup* parent;
			};

			ChunkFileResult_t( *LoadVisGroupCallback )( CChunkFile*, SubLoadVisData* ) = []( CChunkFile* pFile, SubLoadVisData* pLoadData ) -> ChunkFileResult_t
			{
				const auto& LoadVMF = []( CVisGroup* pGroup, CChunkFile* pFile, SubLoadVisData* pLoadData ) -> ChunkFileResult_t
				{
					SubLoadVisData data { pLoadData->callback, pLoadData->data, pGroup };

					CChunkHandlerMap handlers;
					handlers.AddHandler( "visgroup", pLoadData->callback, &data );

					pFile->PushHandlers( &handlers );
					const ChunkFileResult_t eResult = pFile->ReadChunk( CVisGroup::LoadKeyCallback, pGroup );
					pFile->PopHandlers();

					return eResult;
				};
				CVisGroup* pVisGroup = new CVisGroup;
				const ChunkFileResult_t eResult = LoadVMF( pVisGroup, pFile, pLoadData );
				if ( eResult == ChunkFile_Ok )
				{
					if ( pLoadData->parent != nullptr )
					{
						pLoadData->parent->AddChild( pVisGroup );
						pVisGroup->SetParent( pLoadData->parent );
					}

					if ( !pVisGroup->IsAutoVisGroup() )
						pLoadData->data.visGroups.AddToTail( pVisGroup );
				}
				else
					delete pVisGroup;

				return eResult;
			};

			SubLoadVisData subData { LoadVisGroupCallback, *pDoc, nullptr };

			//
			// Set up handlers for the subchunks that we are interested in.
			//
			CChunkHandlerMap handlers;
			handlers.AddHandler( "visgroup", LoadVisGroupCallback, &subData );

			pFile->PushHandlers( &handlers );
			const ChunkFileResult_t eResult = pFile->ReadChunk();
			pFile->PopHandlers();

			return eResult;
		};

		SubLoadHiddenData subLoadHidden { collapseData, LoadEntityCallback };

		CChunkHandlerMap handlers;
		handlers.AddHandler( "world", LoadWorldCallback, &collapseData );
		handlers.AddHandler( "hidden", LoadHiddenCallback, &subLoadHidden );
		handlers.AddHandler( "entity", LoadEntityCallback, &collapseData );
		handlers.AddHandler( "visgroups", LoadVisGroupsCallback, &collapseData );
		handlers.SetErrorHandler( []( CChunkFile*, const char*, void* ) { return false; }, nullptr );

		file.PushHandlers( &handlers );

		while ( eResult == ChunkFile_Ok )
			eResult = file.ReadChunk();

		if ( eResult == ChunkFile_EOF )
			eResult = ChunkFile_Ok;

		file.PopHandlers();
	}

	if ( eResult == ChunkFile_Ok )
	{
		for ( CMapClass* child : std::as_const( collapseData.newChildren ) )
			child->Transform( m_matTransform );
		if ( bRecursive )
		{
			CUtlVector<CMapClass*> toRemove;
			for ( CMapClass* child : std::as_const( collapseData.newChildren ) )
			{
				for ( CMapClass* subChild : std::as_const( *child->GetChildren() ) )
				{
					if ( subChild && subChild->IsMapClass( MAPCLASS_TYPE( CMapInstance ) ) )
					{
						if ( static_cast<CMapInstance*>( subChild )->Collapse( bRecursive, collapseData ) )
							toRemove.AddToTail( child );
						break;
					}
				}
			}
			for ( CMapClass* c : toRemove )
			{
				collapseData.newChildren.FindAndRemove( c );
				delete c;
			}
		}
		return true;
	}

	// Cleanup if failed
	CUtlVector<CMapClass*> del;
#undef GetObject // fu
	if ( chiCount == 0 )
	{
		for ( const auto& c : collapseData.newChildren )
			del.AddToTail( c.GetObject() );
		collapseData.newChildren.RemoveAll();
	}
	else
	{
		for ( int i = chiCount; i < collapseData.newChildren.Count(); ++i )
			del.AddToTail( collapseData.newChildren[i] );
		while ( chiCount < collapseData.newChildren.Count() )
			collapseData.newChildren.Remove( collapseData.newChildren.Count() - 1 );
	}
	del.PurgeAndDeleteElements(); // cannot delete until CMapObjectList is empty

	if ( visCount == 0 )
		collapseData.visGroups.PurgeAndDeleteElements();
	else
	{
		for ( int i = visCount; i < collapseData.visGroups.Count(); ++i )
			delete collapseData.visGroups[i];
		collapseData.visGroups.RemoveMultipleFromTail( collapseData.visGroups.Count() - visCount );
	}

	return false;
}