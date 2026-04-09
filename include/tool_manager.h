/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_MANAGER_H
#define TOOL_MANAGER_H

#include "tools.h"

/**
 * Tool Manager - manages tool registry, activation, and lifecycle
 */

/**
 * Create a new tool manager instance
 * @return Newly created ToolRegistry
 */
ToolRegistry* tool_manager_new(void);

/**
 * Initialize default tools
 * @param manager The tool manager/registry
 * @return TRUE on success, FALSE on failure
 */
gboolean tool_manager_init_defaults(ToolRegistry *manager);

/**
 * Register a tool with the manager
 * @param manager The tool manager
 * @param tool The tool to register
 * @param type The tool type
 * @return TRUE on success, FALSE on failure
 */
gboolean tool_manager_register(ToolRegistry *manager, Tool *tool, ToolType type);

/**
 * Activate a tool by type
 * @param manager The tool manager
 * @param type The tool type to activate
 * @return TRUE on success, FALSE on failure
 */
gboolean tool_manager_activate(ToolRegistry *manager, ToolType type);

/**
 * Get the currently active tool
 * @param manager The tool manager
 * @return The active tool, or NULL if none
 */
Tool* tool_manager_get_active(ToolRegistry *manager);

/**
 * Get a tool by type
 * @param manager The tool manager
 * @param type The tool type
 * @return The tool, or NULL if not found
 */
Tool* tool_manager_get(ToolRegistry *manager, ToolType type);

/**
 * Free the tool manager and all tools
 * @param manager The tool manager to free
 */
void tool_manager_free(ToolRegistry *manager);

#endif /* TOOL_MANAGER_H */

