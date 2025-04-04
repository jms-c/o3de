/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */


#pragma once

//forward declarations.
#include <LyShine/Animation/IUiAnimation.h>
#include "UiAnimSerialize.h"
#include <AzCore/RTTI/TypeInfo.h>

/** General templated track for event type keys.
        KeyType class must be derived from IKey.
*/
template <class KeyType>
class TUiAnimTrack
    : public IUiAnimTrack
{
public:
    AZ_CLASS_ALLOCATOR(TUiAnimTrack, AZ::SystemAllocator, 0)
    AZ_RTTI((TUiAnimTrack<KeyType>, "{5513FA16-991D-40DD-99B2-9C5531AC872C}", KeyType), IUiAnimTrack);

    TUiAnimTrack();

    virtual EUiAnimCurveType GetCurveType() { return eUiAnimCurveType_Unknown; };
    virtual EUiAnimValue GetValueType() { return eUiAnimValue_Unknown; }

    virtual int GetSubTrackCount() const { return 0; };
    virtual IUiAnimTrack* GetSubTrack([[maybe_unused]] int nIndex) const { return 0; };
    virtual const char* GetSubTrackName([[maybe_unused]] int nIndex) const { return NULL; };
    virtual void SetSubTrackName([[maybe_unused]] int nIndex, [[maybe_unused]] const char* name) { assert(0); }

    virtual const CUiAnimParamType&  GetParameterType() const { return m_nParamType; };
    virtual void SetParameterType(CUiAnimParamType type) { m_nParamType = type; };

    virtual const UiAnimParamData& GetParamData() const { return m_componentParamData; }
    virtual void SetParamData(const UiAnimParamData& param) { m_componentParamData = param; }

    //////////////////////////////////////////////////////////////////////////
    // for intrusive_ptr support
    void add_ref() override;
    void release() override;
    //////////////////////////////////////////////////////////////////////////

    virtual bool IsKeySelected(int key) const
    {
        AZ_Assert(key >= 0 && key < (int)m_keys.size(), "Key index is out of range");
        if (m_keys[key].flags & AKEY_SELECTED)
        {
            return true;
        }
        return false;
    }

    virtual void SelectKey(int key, bool select)
    {
        AZ_Assert(key >= 0 && key < (int)m_keys.size(), "Key index is out of range");
        if (select)
        {
            m_keys[key].flags |= AKEY_SELECTED;
        }
        else
        {
            m_keys[key].flags &= ~AKEY_SELECTED;
        }
    }

    //! Return number of keys in track.
    virtual int GetNumKeys() const { return static_cast<int>(m_keys.size()); };

    //! Return true if keys exists in this track
    virtual bool HasKeys() const { return !m_keys.empty(); }

    //! Set number of keys in track.
    //! If needed adds empty keys at end or remove keys from end.
    virtual void SetNumKeys(int numKeys) { m_keys.resize(numKeys); };

    //! Remove specified key.
    virtual void RemoveKey(int num);

    int CreateKey(float time);
    int CloneKey(int fromKey);
    int CopyKey(IUiAnimTrack* pFromTrack, int nFromKey);

    //! Get key at specified location.
    //! @param key Must be valid pointer to compatible key structure, to be filled with specified key location.
    virtual void GetKey(int index, IKey* key) const;

    //! Get time of specified key.
    //! @return key time.
    virtual float GetKeyTime(int index) const;

    //! Find key at given time.
    //! @return Index of found key, or -1 if key with this time not found.
    virtual int FindKey(float time);

    //! Get flags of specified key.
    //! @return key time.
    virtual int GetKeyFlags(int index);

    //! Set key at specified location.
    //! @param key Must be valid pointer to compatible key structure.
    virtual void SetKey(int index, IKey* key);

    //! Set time of specified key.
    virtual void SetKeyTime(int index, float time);

    //! Set flags of specified key.
    virtual void SetKeyFlags(int index, int flags);

    //! Sort keys in track (after time of keys was modified).
    virtual void SortKeys();

    //! Get track flags.
    virtual int GetFlags() { return m_flags; }

    //! Check if track is masked
    virtual bool IsMasked([[maybe_unused]] const uint32 mask) const { return false; }

    //! Set track flags.
    virtual void SetFlags(int flags) { m_flags = flags; }

    //////////////////////////////////////////////////////////////////////////
    // Get track value at specified time.
    // Interpolates keys if needed.
    //////////////////////////////////////////////////////////////////////////
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] float& value) { assert(0); };
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] Vec3& value) { assert(0); };
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] Vec4& value) { assert(0); };
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] Quat& value) { assert(0); };
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] bool& value) { assert(0); };
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] AZ::Vector2& value) { assert(0); };
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] AZ::Vector3& value) { assert(0); };
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] AZ::Vector4& value) { assert(0); };
    virtual void GetValue([[maybe_unused]] float time, [[maybe_unused]] AZ::Color& value) { assert(0); };

    //////////////////////////////////////////////////////////////////////////
    // Set track value at specified time.
    // Adds new keys if required.
    //////////////////////////////////////////////////////////////////////////
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const float& value, [[maybe_unused]] bool bDefault = false) { assert(0); };
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const Vec3& value, [[maybe_unused]] bool bDefault = false) { assert(0); };
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const Vec4& value, [[maybe_unused]] bool bDefault = false) { assert(0); };
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const Quat& value, [[maybe_unused]] bool bDefault = false) { assert(0); };
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const bool& value, [[maybe_unused]] bool bDefault = false) { assert(0); };
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const AZ::Vector2& value, [[maybe_unused]] bool bDefault = false) { assert(0); };
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const AZ::Vector3& value, [[maybe_unused]] bool bDefault = false) { assert(0); };
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const AZ::Vector4& value, [[maybe_unused]] bool bDefault = false) { assert(0); };
    virtual void SetValue([[maybe_unused]] float time, [[maybe_unused]] const AZ::Color& value, [[maybe_unused]] bool bDefault = false) { assert(0); };

    virtual void OffsetKeyPosition([[maybe_unused]] const Vec3& value) { assert(0); };

    /** Assign active time range for this track.
    */
    virtual void SetTimeRange(const Range& timeRange) { m_timeRange = timeRange; };

    /** Serialize this animation track to XML.
            Do not override this method, prefer to override SerializeKey.
    */
    virtual bool Serialize(IUiAnimationSystem* uiAnimationSystem, XmlNodeRef& xmlNode, bool bLoading, bool bLoadEmptyTracks = true);

    virtual bool SerializeSelection(XmlNodeRef& xmlNode, bool bLoading, bool bCopySelected = false, float fTimeOffset = 0);


    /** Serialize single key of this track.
            Override this in derived classes.
            Do not save time attribute, it is already saved in Serialize of the track.
    */
    virtual void SerializeKey(KeyType& key, XmlNodeRef& keyNode, bool bLoading) = 0;

    //////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    /** Get last key before specified time.
            @return Index of key, or -1 if such key not exist.
    */
    int GetActiveKey(float time, KeyType* key);

#ifdef UI_ANIMATION_SYSTEM_SUPPORT_EDITING
    virtual ColorB GetCustomColor() const
    { return m_customColor; }
    virtual void SetCustomColor(ColorB color)
    {
        m_customColor = color;
        m_bCustomColorSet = true;
    }
    virtual bool HasCustomColor() const
    { return m_bCustomColorSet; }
    virtual void ClearCustomColor()
    { m_bCustomColorSet = false; }
#endif

    virtual void GetKeyValueRange(float& fMin, float& fMax) const { fMin = m_fMinKeyValue; fMax = m_fMaxKeyValue; };
    virtual void SetKeyValueRange(float fMin, float fMax){ m_fMinKeyValue = fMin; m_fMaxKeyValue = fMax; };

    static void Reflect(AZ::SerializeContext* serializeContext) {}

protected:
    void CheckValid()
    {
        if (m_bModified)
        {
            SortKeys();
        }
    };
    void Invalidate() { m_bModified = 1; };

    int m_refCount;

    typedef AZStd::vector<KeyType> Keys;
    Keys m_keys;
    Range m_timeRange;

    CUiAnimParamType m_nParamType;
    unsigned int m_currKey : 31;
    unsigned int m_bModified : 1;
    float m_lastTime;
    int m_flags;

    static constexpr unsigned int InvalidKey = 0x7FFFFFFF;

    UiAnimParamData m_componentParamData;

#ifdef UI_ANIMATION_SYSTEM_SUPPORT_EDITING
    ColorB m_customColor;
    bool m_bCustomColorSet;
#endif

    float m_fMinKeyValue;
    float m_fMaxKeyValue;
};

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline TUiAnimTrack<KeyType>::TUiAnimTrack()
    : m_refCount(0)
{
    m_currKey = 0;
    m_flags = 0;
    m_lastTime = -1;
    m_bModified = 0;

#ifdef UI_ANIMATION_SYSTEM_SUPPORT_EDITING
    m_bCustomColorSet = false;
#endif
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline void TUiAnimTrack<KeyType>::add_ref()
{
    ++m_refCount;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline void TUiAnimTrack<KeyType>::release()
{
    if (--m_refCount <= 0)
    {
        delete this;
    }
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline void TUiAnimTrack<KeyType>::RemoveKey(int index)
{
    AZ_Assert(index >= 0 && index < (int)m_keys.size(), "Key index is out of range");
    m_keys.erase(m_keys.begin() + index);
    Invalidate();
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline void TUiAnimTrack<KeyType>::GetKey(int index, IKey* key) const
{
    AZ_Assert(index >= 0 && index < (int)m_keys.size(), "Key index is out of range");
    AZ_Assert(key != 0, "Key cannot be null!");
    *(KeyType*)key = m_keys[index];
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline void TUiAnimTrack<KeyType>::SetKey(int index, IKey* key)
{
    AZ_Assert(index >= 0 && index < (int)m_keys.size(), "Key index is out of range");
    AZ_Assert(key != 0, "Key cannot be null!");
    m_keys[index] = *(KeyType*)key;
    Invalidate();
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline float TUiAnimTrack<KeyType>::GetKeyTime(int index) const
{
    AZ_Assert(index >= 0 && index < (int)m_keys.size(), "Key index is out of range");
    return m_keys[index].time;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline void TUiAnimTrack<KeyType>::SetKeyTime(int index, float time)
{
    AZ_Assert(index >= 0 && index < (int)m_keys.size(), "Key index is out of range");
    m_keys[index].time = time;
    Invalidate();
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline int TUiAnimTrack<KeyType>::FindKey(float time)
{
    for (int i = 0; i < (int)m_keys.size(); i++)
    {
        if (m_keys[i].time == time)
        {
            return i;
        }
    }
    return -1;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline int TUiAnimTrack<KeyType>::GetKeyFlags(int index)
{
    AZ_Assert(index >= 0 && index < (int)m_keys.size(), "Key index is out of range");
    return m_keys[index].flags;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline void TUiAnimTrack<KeyType>::SetKeyFlags(int index, int flags)
{
    AZ_Assert(index >= 0 && index < (int)m_keys.size(), "Key index is out of range");
    m_keys[index].flags = flags;
    Invalidate();
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline void TUiAnimTrack<KeyType>::SortKeys()
{
    std::sort(m_keys.begin(), m_keys.end());
    m_bModified = 0;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline bool TUiAnimTrack<KeyType>::Serialize([[maybe_unused]] IUiAnimationSystem* uiAnimationSystem, XmlNodeRef& xmlNode, bool bLoading, bool bLoadEmptyTracks)
{
    if (bLoading)
    {
        int num = xmlNode->getChildCount();

        Range timeRange;
        int flags = m_flags;
        xmlNode->getAttr("Flags", flags);
        xmlNode->getAttr("StartTime", timeRange.start);
        xmlNode->getAttr("EndTime", timeRange.end);
        SetFlags(flags);
        SetTimeRange(timeRange);

#ifdef UI_ANIMATION_SYSTEM_SUPPORT_EDITING
        xmlNode->getAttr("HasCustomColor", m_bCustomColorSet);
        if (m_bCustomColorSet)
        {
            unsigned int abgr;
            xmlNode->getAttr("CustomColor", abgr);
            m_customColor = ColorB(abgr);
        }
#endif

        SetNumKeys(num);
        for (int i = 0; i < num; i++)
        {
            XmlNodeRef keyNode = xmlNode->getChild(i);
            keyNode->getAttr("time", m_keys[i].time);

            SerializeKey(m_keys[i], keyNode, bLoading);
        }

        if ((!num) && (!bLoadEmptyTracks))
        {
            return false;
        }
    }
    else
    {
        int num = GetNumKeys();
        CheckValid();
        xmlNode->setAttr("Flags", GetFlags());
        xmlNode->setAttr("StartTime", m_timeRange.start);
        xmlNode->setAttr("EndTime", m_timeRange.end);
#ifdef UI_ANIMATION_SYSTEM_SUPPORT_EDITING
        xmlNode->setAttr("HasCustomColor", m_bCustomColorSet);
        if (m_bCustomColorSet)
        {
            xmlNode->setAttr("CustomColor", m_customColor.pack_abgr8888());
        }
#endif

        for (int i = 0; i < num; i++)
        {
            XmlNodeRef keyNode = xmlNode->newChild("Key");
            keyNode->setAttr("time", m_keys[i].time);

            SerializeKey(m_keys[i], keyNode, bLoading);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline bool TUiAnimTrack<KeyType>::SerializeSelection(XmlNodeRef& xmlNode, bool bLoading, bool bCopySelected, float fTimeOffset)
{
    if (bLoading)
    {
        int numCur = GetNumKeys();
        int num = xmlNode->getChildCount();

        int type;
        xmlNode->getAttr("TrackType", type);

        if (type != GetCurveType())
        {
            return false;
        }

        SetNumKeys(num + numCur);
        for (int i = 0; i < num; i++)
        {
            XmlNodeRef keyNode = xmlNode->getChild(i);
            keyNode->getAttr("time", m_keys[i + numCur].time);
            m_keys[i + numCur].time += fTimeOffset;

            SerializeKey(m_keys[i + numCur], keyNode, bLoading);
            if (bCopySelected)
            {
                m_keys[i + numCur].flags |= AKEY_SELECTED;
            }
        }
        SortKeys();
    }
    else
    {
        int num = GetNumKeys();
        xmlNode->setAttr("TrackType", GetCurveType());

        //CheckValid();

        for (int i = 0; i < num; i++)
        {
            if (!bCopySelected || GetKeyFlags(i) &   AKEY_SELECTED)
            {
                XmlNodeRef keyNode = xmlNode->newChild("Key");
                keyNode->setAttr("time", m_keys[i].time);

                SerializeKey(m_keys[i], keyNode, bLoading);
            }
        }
    }
    return true;
}



//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline int TUiAnimTrack<KeyType>::CreateKey(float time)
{
    KeyType key, akey;
    int nkey = GetNumKeys();
    SetNumKeys(nkey + 1);
    key.time = time;
    SetKey(nkey, &key);

    return nkey;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline int TUiAnimTrack<KeyType>::CloneKey(int fromKey)
{
    KeyType key;
    GetKey(fromKey, &key);
    int nkey = GetNumKeys();
    SetNumKeys(nkey + 1);
    SetKey(nkey, &key);
    return nkey;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline int TUiAnimTrack<KeyType>::CopyKey(IUiAnimTrack* pFromTrack, int nFromKey)
{
    KeyType key;
    pFromTrack->GetKey(nFromKey, &key);
    int nkey = GetNumKeys();
    SetNumKeys(nkey + 1);
    SetKey(nkey, &key);
    return nkey;
}

//////////////////////////////////////////////////////////////////////////
template <class KeyType>
inline int TUiAnimTrack<KeyType>::GetActiveKey(float time, KeyType* key)
{
    CheckValid();

    if (key == NULL)
    {
        return -1;
    }

    int nkeys = static_cast<int>(m_keys.size());
    if (nkeys == 0)
    {
        m_lastTime = time;
        m_currKey = InvalidKey;
        return m_currKey;
    }

    bool bTimeWrap = false;

    if ((m_flags & eUiAnimTrackFlags_Cycle) || (m_flags & eUiAnimTrackFlags_Loop))
    {
        // Warp time.
        const char* desc = 0;
        float duration = 0;
        GetKeyInfo(nkeys - 1, desc, duration);
        float endtime = GetKeyTime(nkeys - 1) + duration;
        time = fmod_tpl(time, endtime);
        if (time < m_lastTime)
        {
            // Time is wrapped.
            bTimeWrap = true;
        }
    }
    m_lastTime = time;

    // Time is before first key.
    if (m_keys[0].time > time)
    {
        if (bTimeWrap)
        {
            // If time wrapped, active key is last key.
            m_currKey = nkeys - 1;
            *key = m_keys[m_currKey];
        }
        else
        {
            m_currKey = InvalidKey;
        }
        return m_currKey;
    }

    if (m_currKey < 0)
    {
        m_currKey = 0;
    }

    // Start from current key.
    int i;
    for (i = m_currKey; i < nkeys; i++)
    {
        if (time >= m_keys[i].time)
        {
            if ((i >= nkeys - 1) || (time < m_keys[i + 1].time))
            {
                m_currKey = i;
                *key = m_keys[m_currKey];
                return m_currKey;
            }
        }
        else
        {
            break;
        }
    }

    // Start from begining.
    for (i = 0; i < nkeys; i++)
    {
        if (time >= m_keys[i].time)
        {
            if ((i >= nkeys - 1) || (time < m_keys[i + 1].time))
            {
                m_currKey = i;
                *key = m_keys[m_currKey];
                return m_currKey;
            }
        }
        else
        {
            break;
        }
    }
    m_currKey = InvalidKey;
    return m_currKey;
}
