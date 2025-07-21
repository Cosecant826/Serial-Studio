/*
 * Serial Studio
 * https://serial-studio.com/
 *
 * Copyright (C) 2020–2025 Alex Spataru
 *
 * This file is dual-licensed:
 *
 * - Under the GNU GPLv3 (or later) for builds that exclude Pro modules.
 * - Under the Serial Studio Commercial License for builds that include
 *   any Pro functionality.
 *
 * You must comply with the terms of one of these licenses, depending
 * on your use case.
 *
 * For GPL terms, see <https://www.gnu.org/licenses/gpl-3.0.html>
 * For commercial terms, see LICENSE_COMMERCIAL.md in the project root.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-SerialStudio-Commercial
 */

#include "JSON/Frame.h"
#include "SerialStudio.h"

/**
 * @brief Reads a value from a QJsonObject based on a key, returning a default
 *        value if the key does not exist.
 *
 * This function checks if the given key exists in the provided QJsonObject.
 * If the key is found, it returns the associated value. Otherwise, it returns
 * the specified default value.
 *
 * @param object The QJsonObject to read the data from.
 * @param key The key to look for in the QJsonObject.
 * @param defaultValue The value to return if the key is not found in the
 * QJsonObject.
 * @return The value associated with the key, or the defaultValue if the key is
 * not present.
 */
static QVariant SAFE_READ(const QJsonObject &object, const QString &key,
                          const QVariant &defaultValue)
{
  if (object.contains(key))
    return object.value(key);

  return defaultValue;
}

/**
 * Constructor function
 */
JSON::Frame::Frame()
  : m_containsCommercialFeatures(false)
{
}

/**
 * Destructor function, free memory used by the @c Group objects before
 * destroying an instance of this class.
 */
JSON::Frame::~Frame()
{
  m_groups.clear();
  m_actions.clear();
  m_groups.squeeze();
  m_actions.squeeze();
}

/**
 * Resets the frame title and frees the memory used by the @c Group objects
 * associated to the instance of the @c Frame object.
 */
void JSON::Frame::clear()
{
  m_title = "";
  m_frameEnd = "";
  m_checksum = "";
  m_frameStart = "";
  m_containsCommercialFeatures = false;

  m_groups.clear();
  m_actions.clear();
  m_groups.squeeze();
  m_actions.squeeze();
}

/**
 * @brief Returns @c true if the project has a defined title and it has at least
 *        one dataset group.
 */
bool JSON::Frame::isValid() const
{
  return !title().isEmpty() && groupCount() > 0;
}

/**
 * @brief Assigns globally unique identifiers to all datasets in the frame.
 *
 * This method iterates over all groups and their corresponding datasets,
 * assigning each dataset a unique ID starting from 1. These IDs are used
 * internally by the dashboard for unambiguous dataset tracking across groups,
 * especially when multiple datasets share the same logical index or title.
 *
 * Call this function after the frame structure has been fully initialized.
 */
void JSON::Frame::buildUniqueIds()
{
  quint32 id = 1;
  for (int i = 0; i < m_groups.count(); ++i)
  {
    for (int j = 0; j < m_groups[i].datasetCount(); ++j)
    {
      m_groups[i].m_datasets[j].setUniqueId(id);
      ++id;
    }
  }
}

/**
 * @brief Compares the structural equivalence of two JSON::Frame objects.
 *
 * This method checks whether the current frame and the given frame have the
 * same number of groups, and whether each corresponding group has the same
 * group ID and the same number of datasets in the same order, with matching
 * dataset indices.
 *
 * @param other The frame to compare against.
 * @return true if both frames have identical structural layout; false
 * otherwise.
 */
bool JSON::Frame::equalsStructure(const JSON::Frame &other) const
{
  const int groupCountA = groupCount();
  const int groupCountB = other.groupCount();
  if (groupCountA != groupCountB)
    return false;

  const auto &groupsA = groups();
  const auto &groupsB = other.groups();
  for (int i = 0; i < groupCountA; ++i)
  {
    const auto &g1 = groupsA[i];
    const auto &g2 = groupsB[i];

    if (g1.groupId() != g2.groupId())
      return false;

    const auto &datasetsA = g1.datasets();
    const auto &datasetsB = g2.datasets();

    const int datasetCount = datasetsA.size();
    if (datasetCount != datasetsB.size())
      return false;

    for (int j = 0; j < datasetCount; ++j)
    {
      if (datasetsA[j].index() != datasetsB[j].index())
        return false;
    }
  }

  return true;
}

/**
 * @brief Serializes the frame information and its data into a JSON object.
 *
 * @return A QJsonObject containing the group's properties and an array of
 * encoded datasets.
 */
QJsonObject JSON::Frame::serialize() const
{
  QJsonArray groupArray;
  for (const auto &group : m_groups)
    groupArray.append(group.serialize());

  QJsonArray actionArray;
  for (const auto &action : m_actions)
    actionArray.append(action.serialize());

  QJsonObject object;
  object.insert(QStringLiteral("title"), m_title.simplified());
  object.insert(QStringLiteral("groups"), groupArray);
  object.insert(QStringLiteral("actions"), actionArray);
  return object;
}

/**
 * Reads the frame information and all its asociated groups (and datatsets) from
 * the given JSON @c object.
 *
 * @return @c true on success, @c false on failure
 */
bool JSON::Frame::read(const QJsonObject &object)
{
  // Reset frame data
  clear();

  // Get title & groups array
  const auto groups = object.value(QStringLiteral("groups")).toArray();
  const auto actions = object.value(QStringLiteral("actions")).toArray();
  const auto title = SAFE_READ(object, "title", "").toString().simplified();

  // We need to have a project title and at least one group
  if (!title.isEmpty() && !groups.isEmpty())
  {
    // Update title
    m_title = title;

    // Obtain frame delimiters
    auto fEndStr = SAFE_READ(object, "frameEnd", "").toString();
    auto fStartStr = SAFE_READ(object, "frameStart", "").toString();
    auto isHex = SAFE_READ(object, "hexadecimalDelimiters", false).toBool();

    // Read checksum method
    m_checksum = SAFE_READ(object, "checksum", "").toString();

    // Convert hex + escape strings (e.g. "0A 0D", or "\\n0D") to raw bytes
    if (isHex)
    {
      QString resolvedEnd = SerialStudio::resolveEscapeSequences(fEndStr);
      QString resolvedStart = SerialStudio::resolveEscapeSequences(fStartStr);
      m_frameEnd = QByteArray::fromHex(resolvedEnd.remove(' ').toUtf8());
      m_frameStart = QByteArray::fromHex(resolvedStart.remove(' ').toUtf8());
    }

    // Resolve escape sequences (e.g. "\\n") and encode to UTF-8 bytes
    else
    {
      m_frameEnd = SerialStudio::resolveEscapeSequences(fEndStr).toUtf8();
      m_frameStart = SerialStudio::resolveEscapeSequences(fStartStr).toUtf8();
    }

    // Generate groups & datasets from data frame
    for (auto i = 0; i < groups.count(); ++i)
    {
      Group group(i);
      if (group.read(groups.at(i).toObject()))
        m_groups.append(group);
    }

    // Generate actions from data frame
    for (auto i = 0; i < actions.count(); ++i)
    {
      Action action;
      if (action.read(actions.at(i).toObject()))
        m_actions.append(action);
    }

    // Check if any of the groups contains commercial widgets
    m_containsCommercialFeatures = SerialStudio::commercialCfg(m_groups);

    // Build unique dataset IDs
    buildUniqueIds();

    // Return status
    return groupCount() > 0;
  }

  // Error
  clear();
  return false;
}

/**
 * Returns the number of groups contained in the frame.
 */
int JSON::Frame::groupCount() const
{
  return static_cast<int>(m_groups.size());
}

/**
 * Returns @c true if the frame contains features that should only be enabled
 * for commercial users with a valid license, such as the 3D plot widget.
 */
bool JSON::Frame::containsCommercialFeatures() const
{
  return m_containsCommercialFeatures;
}

/**
 * Returns the title of the frame.
 */
const QString &JSON::Frame::title() const
{
  return m_title;
}

/**
 * Returns the name of the checksum method to use (optional)
 */
const QString &JSON::Frame::checksum() const
{
  return m_checksum;
}

/**
 * Returns the frame end sequence.
 */
const QByteArray &JSON::Frame::frameEnd() const
{
  return m_frameEnd;
}

/**
 * Returns the frame start sequence.
 */
const QByteArray &JSON::Frame::frameStart() const
{
  return m_frameStart;
}

/**
 * Returns a vector of pointers to the @c Group objects associated to this
 * frame.
 */
const QVector<JSON::Group> &JSON::Frame::groups() const
{
  return m_groups;
}

/**
 * Returns a vector of pointers to the @c Action objects associated to this
 * frame.
 */
const QVector<JSON::Action> &JSON::Frame::actions() const
{
  return m_actions;
}
