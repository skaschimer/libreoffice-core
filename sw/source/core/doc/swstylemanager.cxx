/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include "swstylemanager.hxx"
#include <svl/stylepool.hxx>
#include <istyleaccess.hxx>
#include <swatrset.hxx>
#include <ndhints.hxx>
#include <unordered_map>
#include <optional>
#include <osl/diagnose.h>
#include <names.hxx>

typedef std::unordered_map< OUString,
                            std::shared_ptr<SfxItemSet> > SwStyleNameCache;

namespace {

class SwStyleCache
{
    SwStyleNameCache mMap;
public:
    SwStyleCache() {}
    void addStyleName( const std::shared_ptr<SfxItemSet>& pStyle )
        { mMap[ StylePool::nameOf(pStyle) ] = pStyle; }
    void addCompletePool( StylePool& rPool );
    std::shared_ptr<SfxItemSet> getByName( const OUString& rName ) { return mMap[rName]; }
    void clear() { mMap.clear(); }
};

}

void SwStyleCache::addCompletePool( StylePool& rPool )
{
    rPool.populateCacheMap(mMap);
}

namespace {

class SwStyleManager : public IStyleAccess
{
    StylePool m_aAutoCharPool;
    StylePool m_aAutoParaPool;
    SwStyleCache maCharCache;
    SwStyleCache maParaCache;

public:
    // accept empty item set for ignorable paragraph items.
    explicit SwStyleManager(SfxItemSet const* pIgnorableParagraphItems)
        : m_aAutoParaPool(pIgnorableParagraphItems)
    {}
    virtual std::shared_ptr<SfxItemSet> getAutomaticStyle( const SfxItemSet& rSet,
                                                               IStyleAccess::SwAutoStyleFamily eFamily,
                                                               const OUString* pParentName = nullptr ) override;
    virtual std::shared_ptr<SwAttrSet> getAutomaticStyle( const SwAttrSet& rSet,
                                                               IStyleAccess::SwAutoStyleFamily eFamily,
                                                               const ProgName* pParentName = nullptr ) override;
    virtual std::shared_ptr<SfxItemSet> getByName( const OUString& rName,
                                                               IStyleAccess::SwAutoStyleFamily eFamily ) override;
    virtual void getAllStyles( std::vector<std::shared_ptr<SfxItemSet>> &rStyles,
                                                               IStyleAccess::SwAutoStyleFamily eFamily ) override;
    virtual std::shared_ptr<SfxItemSet> cacheAutomaticStyle( const SfxItemSet& rSet,
                                                               SwAutoStyleFamily eFamily ) override;
    virtual void clearCaches() override;
};

}

std::unique_ptr<IStyleAccess> createStyleManager( SfxItemSet const * pIgnorableParagraphItems )
{
    return std::make_unique<SwStyleManager>( pIgnorableParagraphItems );
}

void SwStyleManager::clearCaches()
{
    maCharCache.clear();
    maParaCache.clear();
}

namespace
{
// tdf#172647: automatic styles bypass the SwFormat/hint conversion paths, so
// convert legacy font names to their typographic form here, before the style is
// pooled, keeping the whole model keyed by typographic identity. Clone() keeps
// the concrete (SwAttrSet) type the paragraph autostyle pool requires.
std::shared_ptr<SfxItemSet> insertTypographicItemSet(StylePool& rPool, const SfxItemSet& rSet,
                                                     const OUString* pParentName)
{
    const auto* pPool = dynamic_cast<const SwAttrPool*>(rSet.GetPool());
    std::optional<SfxItemSet> oConverted
        = pPool ? ConvertCharFontsToTypographic(rSet, pPool->GetDoc()) : std::nullopt;
    if (!oConverted)
        return rPool.insertItemSet(rSet, pParentName);
    std::unique_ptr<SfxItemSet> pConverted = rSet.Clone();
    pConverted->Put(*oConverted);
    return rPool.insertItemSet(*pConverted, pParentName);
}
}

std::shared_ptr<SfxItemSet> SwStyleManager::getAutomaticStyle( const SfxItemSet& rSet,
                                                                   IStyleAccess::SwAutoStyleFamily eFamily,
                                                                   const OUString* pParentName )
{
    assert(eFamily != IStyleAccess::AUTO_STYLE_PARA || dynamic_cast<const SwAttrSet*>(&rSet));
    StylePool& rAutoPool
        = eFamily == IStyleAccess::AUTO_STYLE_CHAR ? m_aAutoCharPool : m_aAutoParaPool;
    return insertTypographicItemSet( rAutoPool, rSet, pParentName );
}

std::shared_ptr<SwAttrSet> SwStyleManager::getAutomaticStyle( const SwAttrSet& rSet,
                                                                   IStyleAccess::SwAutoStyleFamily eFamily,
                                                                   const ProgName* pParentName )
{
    StylePool& rAutoPool
        = eFamily == IStyleAccess::AUTO_STYLE_CHAR ? m_aAutoCharPool : m_aAutoParaPool;
    std::shared_ptr<SfxItemSet> pItemSet = insertTypographicItemSet( rAutoPool, rSet, pParentName ? &pParentName->toString() : nullptr );
    std::shared_ptr<SwAttrSet> pAttrSet = std::dynamic_pointer_cast<SwAttrSet>(pItemSet);
    assert(bool(pItemSet) == bool(pAttrSet) && "types do not match");
    return pAttrSet;
}

std::shared_ptr<SfxItemSet> SwStyleManager::cacheAutomaticStyle( const SfxItemSet& rSet,
                                                                   IStyleAccess::SwAutoStyleFamily eFamily )
{
    assert(eFamily != IStyleAccess::AUTO_STYLE_PARA || dynamic_cast<const SwAttrSet*>(&rSet));
    StylePool& rAutoPool
        = eFamily == IStyleAccess::AUTO_STYLE_CHAR ? m_aAutoCharPool : m_aAutoParaPool;
    std::shared_ptr<SfxItemSet> pStyle = insertTypographicItemSet( rAutoPool, rSet, nullptr );
    if (eFamily == IStyleAccess::AUTO_STYLE_CHAR)
    {
        maCharCache.addStyleName( pStyle );
    }
    else
    {
        maParaCache.addStyleName( pStyle );
    }
    return pStyle;
}

std::shared_ptr<SfxItemSet> SwStyleManager::getByName( const OUString& rName,
                                                           IStyleAccess::SwAutoStyleFamily eFamily )
{
    StylePool& rAutoPool
        = eFamily == IStyleAccess::AUTO_STYLE_CHAR ? m_aAutoCharPool : m_aAutoParaPool;
    SwStyleCache &rCache = eFamily == IStyleAccess::AUTO_STYLE_CHAR ? maCharCache : maParaCache;
    std::shared_ptr<SfxItemSet> pStyle = rCache.getByName( rName );
    if( !pStyle )
    {
        // Ok, ok, it's allowed to ask for uncached styles (from UNO) but it should not be done
        // during loading a document
        OSL_FAIL( "Don't ask for uncached styles" );
        rCache.addCompletePool( rAutoPool );
        pStyle = rCache.getByName( rName );
    }
    assert(!pStyle || eFamily != IStyleAccess::AUTO_STYLE_PARA || dynamic_cast<SwAttrSet*>(pStyle.get()));
    return pStyle;
}

void SwStyleManager::getAllStyles( std::vector<std::shared_ptr<SfxItemSet>> &rStyles,
                                   IStyleAccess::SwAutoStyleFamily eFamily )
{
    StylePool& rAutoPool
        = eFamily == IStyleAccess::AUTO_STYLE_CHAR ? m_aAutoCharPool : m_aAutoParaPool;
    rAutoPool.getAllStyles(rStyles);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
