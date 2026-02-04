/*
 * aesd-circular-buffer.c
 *
 *  Created on: March 1st, 2020
 *      Author: Dan Walkes
 */

#include <string.h>
#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any any
 * user space memory checks (ignore for assignment 7)
 * @param char_offset the position to search for in the buffer list, relative to the start of the
 * buffer list
 * @param entry_offset_byte_rtn used to return the offset within the respective buffer entry
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (out of bounds)
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    size_t current_pos = 0;
    uint8_t index = buffer->out_offs;
    uint8_t count = 0;
    uint8_t max_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // Iterate through the entries starting from out_offs
    while (count < max_entries) {
        if (count > 0 && index == buffer->out_offs && !buffer->full) {
            // We've looped back to the start and the buffer isn't full
            break;
        }
        
        // If we are at an entry that hasn't been filled yet (and buffer is not full)
        if (!buffer->full && index == buffer->in_offs && count != 0) {
            break;
        }

        struct aesd_buffer_entry *entry = &buffer->entry[index];
        
        // Check if the requested offset is within this entry
        if (char_offset < current_pos + entry->size) {
            if (entry_offset_byte_rtn != NULL) {
                *entry_offset_byte_rtn = char_offset - current_pos;
            }
            return entry;
        }

        current_pos += entry->size;
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        count++;

        // If buffer is full, we can check all 10 entries. 
        // If not full, we check until we hit in_offs.
        if (!buffer->full && index == buffer->in_offs) {
            break;
        }
    }

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer, overwriting an existing entry if necessary.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // Add the entry at the current in_offs
    buffer->entry[buffer->in_offs] = *add_entry;

    // Advance in_offs
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // If buffer was already full, we need to advance out_offs as well (overwriting old data)
    if (buffer->full) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Check if the buffer is now full
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    } else {
        buffer->full = false;
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}