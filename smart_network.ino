// ─────────────────────────────────────────────────────────────
// Smart Network Batching - PHASE 4: Intelligent Network Optimization
// Advanced command batching, priority queuing, and adaptive network management
// ─────────────────────────────────────────────────────────────

#if ENABLE_SMART_CACHING // Using smart caching flag for network optimizations

#include <ArduinoJson.h>

// ───── Smart Batching Configuration ─────
#define MAX_BATCH_COMMANDS 8              // Maximum commands in a batch
#define BATCH_TIMEOUT_MS 50               // Batch commands within 50ms
#define PRIORITY_QUEUE_SIZE 16            // Priority queue size
#define ADAPTIVE_RETRY_MAX_COUNT 5        // Maximum adaptive retries
#define COMMAND_DEDUPLICATION_WINDOW_MS 200  // Deduplicate within 200ms
#define NETWORK_CONGESTION_THRESHOLD 500  // Network congestion threshold (ms)

// ───── Command Priority Levels and Batching Types ─────
// NetworkCommandPriority and BatchingStrategy enums are now defined in the main .ino file

// ───── Smart Network Command Structure and Batch Processing Context ─────
// SmartNetworkCommand and CommandBatch structs are now defined in the main .ino file

// ───── Network Performance Metrics ─────
typedef struct {
  uint32_t avgLatency;           // Average network latency
  uint32_t peakLatency;          // Peak latency observed
  uint32_t successRate;          // Success rate percentage
  uint32_t congestionLevel;      // Current congestion level (0-100)
  uint32_t adaptiveRetryDelay;   // Current adaptive retry delay
  uint32_t totalCommandsSent;    // Total commands sent
  uint32_t totalCommandsFailed;  // Total commands failed
  uint32_t batchesProcessed;     // Number of batches processed
  uint32_t commandsDeduplicated; // Commands removed by deduplication
  bool networkCongested;         // Network is currently congested
} NetworkPerformanceMetrics;

// ───── Smart Network Context ─────
typedef struct {
  SmartNetworkCommand priorityQueue[PRIORITY_QUEUE_SIZE];
  uint8_t queueHead, queueTail, queueCount;
  CommandBatch currentBatch;
  NetworkPerformanceMetrics metrics;
  uint32_t lastBatchProcess;
  uint32_t lastCongestionCheck;
  bool adaptiveBatchingEnabled;
  bool intelligentRetryEnabled;
  uint8_t currentBatchSize;      // Adaptive batch size
  uint32_t batchingThreshold;    // Adaptive batching threshold
} SmartNetworkContext;

// ───── Global State ─────
static SmartNetworkContext SNet_context = {0};
static bool SNet_initialized = false;

// Forward declarations
bool NetworkTask_queueCommand(uint8_t type, uint8_t value);
bool WLEDClient_sendCommand(const char* payload);
bool WebSocketClient_isConnected();
bool WebSocketClient_queueMessage(const char* message);
uint32_t NetworkTask_getAvgProcessingTime();
void FreqManager_notifyNetworkActivity();
void PScale_notifyEvent(uint8_t eventType, uint16_t duration, uint8_t intensity);

// ───── Command Classification and Analysis ─────
NetworkCommandPriority SNet_classifyCommand(uint8_t commandType) {
  switch (commandType) {
    case 4: // NET_CMD_POWER_TOGGLE
      return PRIORITY_CRITICAL;
      
    case 1: // NET_CMD_PRESET
    case 2: // NET_CMD_QUICKLOAD
    case 3: // NET_CMD_BRIGHTNESS
      return PRIORITY_HIGH;
      
    case 5: // NET_CMD_PRESET_CYCLE
    case 6: // NET_CMD_PALETTE_CYCLE
      return PRIORITY_NORMAL;
      
    case 7: // NET_CMD_SYNC_STATE
    case 8: // NET_CMD_WIFI_RECONNECT
      return PRIORITY_LOW;
      
    default:
      return PRIORITY_NORMAL;
  }
}

BatchingStrategy SNet_getBatchingStrategy(uint8_t commandType) {
  switch (commandType) {
    case 1: // NET_CMD_PRESET
    case 2: // NET_CMD_QUICKLOAD
      return BATCH_MERGE; // Multiple preset changes can be merged to latest
      
    case 3: // NET_CMD_BRIGHTNESS
      return BATCH_MERGE; // Multiple brightness changes can be merged
      
    case 5: // NET_CMD_PRESET_CYCLE
    case 6: // NET_CMD_PALETTE_CYCLE
      return BATCH_SEQUENCE; // Can be sequenced with other commands
      
    case 7: // NET_CMD_SYNC_STATE
      return BATCH_CONSOLIDATE; // Multiple syncs can be consolidated
      
    case 4: // NET_CMD_POWER_TOGGLE
    case 8: // NET_CMD_WIFI_RECONNECT
      return BATCH_NONE; // Cannot be batched
      
    default:
      return BATCH_SEQUENCE;
  }
}

bool SNet_canMergeCommands(SmartNetworkCommand* cmd1, SmartNetworkCommand* cmd2) {
  if (cmd1->commandType != cmd2->commandType) return false;
  if (cmd1->batchStrategy != BATCH_MERGE) return false;
  
  // Commands must be close in time to be mergeable
  uint32_t timeDiff = abs((int32_t)(cmd2->timestamp - cmd1->timestamp));
  return timeDiff <= COMMAND_DEDUPLICATION_WINDOW_MS;
}

// ───── Command Deduplication ─────
void SNet_deduplicateQueue() {
  for (uint8_t i = 0; i < SNet_context.queueCount - 1; i++) {
    SmartNetworkCommand* cmd1 = &SNet_context.priorityQueue[i];
    if (cmd1->timedOut) continue;
    
    for (uint8_t j = i + 1; j < SNet_context.queueCount; j++) {
      SmartNetworkCommand* cmd2 = &SNet_context.priorityQueue[j];
      if (cmd2->timedOut) continue;
      
      if (SNet_canMergeCommands(cmd1, cmd2)) {
        // Keep the newer command, remove the older one
        Serial.printf("[SNET] Deduplicating command type %d: %d -> %d\n", 
                      cmd1->commandType, cmd1->originalValue, cmd2->originalValue);
        
        cmd1->timedOut = true; // Mark older command as timed out
        SNet_context.metrics.commandsDeduplicated++;
        break;
      }
    }
  }
  
  // Remove timed out commands
  SNet_compactQueue();
}

void SNet_compactQueue() {
  uint8_t writeIndex = 0;
  
  for (uint8_t readIndex = 0; readIndex < SNet_context.queueCount; readIndex++) {
    if (!SNet_context.priorityQueue[readIndex].timedOut) {
      if (writeIndex != readIndex) {
        SNet_context.priorityQueue[writeIndex] = SNet_context.priorityQueue[readIndex];
      }
      writeIndex++;
    }
  }
  
  SNet_context.queueCount = writeIndex;
}

// ───── Smart Command Queuing ─────
bool SNet_queueSmartCommand(uint8_t commandType, uint8_t value, uint32_t deadline) {
  if (SNet_context.queueCount >= PRIORITY_QUEUE_SIZE) {
    Serial.println("[SNET] Priority queue full");
    return false;
  }
  
  uint32_t now = millis();
  SmartNetworkCommand* cmd = &SNet_context.priorityQueue[SNet_context.queueCount];
  
  // Initialize command
  cmd->commandType = commandType;
  cmd->priority = SNet_classifyCommand(commandType);
  cmd->batchStrategy = SNet_getBatchingStrategy(commandType);
  cmd->timestamp = now;
  cmd->deadline = deadline ? deadline : (now + 5000); // Default 5s deadline
  cmd->retryCount = 0;
  cmd->lastRetry = 0;
  cmd->estimatedLatency = SNet_context.metrics.avgLatency;
  cmd->canMerge = (cmd->batchStrategy == BATCH_MERGE);
  cmd->timedOut = false;
  cmd->originalValue = value;
  
  // Generate payload based on command type
  switch (commandType) {
    case 1: // NET_CMD_PRESET
      snprintf(cmd->payload, sizeof(cmd->payload), "{\"ps\":%d}", value);
      strcpy(cmd->endpoint, "/json/state");
      break;
      
    case 2: // NET_CMD_QUICKLOAD
      snprintf(cmd->payload, sizeof(cmd->payload), "{\"pd\":%d}", value);
      strcpy(cmd->endpoint, "/json/state");
      break;
      
    case 3: // NET_CMD_BRIGHTNESS
      snprintf(cmd->payload, sizeof(cmd->payload), "{\"bri\":%d}", value);
      strcpy(cmd->endpoint, "/json/state");
      break;
      
    case 4: // NET_CMD_POWER_TOGGLE
      strcpy(cmd->payload, "{\"on\":\"t\"}");
      strcpy(cmd->endpoint, "/json/state");
      break;
      
    case 7: // NET_CMD_SYNC_STATE
      strcpy(cmd->payload, "{\"v\":true}");
      strcpy(cmd->endpoint, "/json/state");
      break;
      
    default:
      snprintf(cmd->payload, sizeof(cmd->payload), "{\"cmd\":%d,\"val\":%d}", commandType, value);
      strcpy(cmd->endpoint, "/json/state");
  }
  
  SNet_context.queueCount++;
  
  Serial.printf("[SNET] Queued command: type=%d, priority=%d, batch=%d, deadline=+%dms\n",
                commandType, cmd->priority, cmd->batchStrategy, deadline - now);
  
  // Sort queue by priority
  SNet_sortQueueByPriority();
  
  return true;
}

void SNet_sortQueueByPriority() {
  // Simple insertion sort by priority (stable sort)
  for (uint8_t i = 1; i < SNet_context.queueCount; i++) {
    SmartNetworkCommand cmd = SNet_context.priorityQueue[i];
    int8_t j = i - 1;
    
    // Higher priority (lower number) commands go first
    while (j >= 0 && SNet_context.priorityQueue[j].priority > cmd.priority) {
      SNet_context.priorityQueue[j + 1] = SNet_context.priorityQueue[j];
      j--;
    }
    
    SNet_context.priorityQueue[j + 1] = cmd;
  }
}

// ───── Intelligent Batching ─────
bool SNet_createBatch() {
  if (SNet_context.queueCount == 0) return false;
  
  CommandBatch* batch = &SNet_context.currentBatch;
  batch->commandCount = 0;
  batch->batchStartTime = millis();
  batch->estimatedBatchTime = 0;
  batch->readyForExecution = false;
  
  uint32_t now = millis();
  
  // Add high priority commands immediately
  for (uint8_t i = 0; i < SNet_context.queueCount && batch->commandCount < MAX_BATCH_COMMANDS; i++) {
    SmartNetworkCommand* cmd = &SNet_context.priorityQueue[i];
    
    if (cmd->timedOut) continue;
    
    // Critical and high priority commands get immediate execution
    if (cmd->priority <= PRIORITY_HIGH) {
      batch->commands[batch->commandCount++] = *cmd;
      batch->estimatedBatchTime += cmd->estimatedLatency;
      cmd->timedOut = true; // Mark as processed
      
      // Critical commands execute immediately
      if (cmd->priority == PRIORITY_CRITICAL) {
        batch->readyForExecution = true;
        break;
      }
    }
    // Normal and low priority commands wait for batching window
    else if (cmd->priority >= PRIORITY_NORMAL) {
      uint32_t commandAge = now - cmd->timestamp;
      
      if (commandAge >= SNet_context.batchingThreshold || 
          (now - cmd->deadline) < 1000) { // Near deadline
        batch->commands[batch->commandCount++] = *cmd;
        batch->estimatedBatchTime += cmd->estimatedLatency;
        cmd->timedOut = true; // Mark as processed
      }
    }
  }
  
  // Check if batch is ready based on various criteria
  if (!batch->readyForExecution) {
    bool timeoutReached = (now - batch->batchStartTime) >= BATCH_TIMEOUT_MS;
    bool batchFull = batch->commandCount >= SNet_context.currentBatchSize;
    bool nearDeadline = false;
    
    // Check if any command is near its deadline
    for (uint8_t i = 0; i < batch->commandCount; i++) {
      if ((now + batch->estimatedBatchTime) >= batch->commands[i].deadline) {
        nearDeadline = true;
        break;
      }
    }
    
    batch->readyForExecution = timeoutReached || batchFull || nearDeadline;
  }
  
  // Remove processed commands from queue
  SNet_compactQueue();
  
  if (batch->commandCount > 0) {
    Serial.printf("[SNET] Created batch: %d commands, %dms estimated, ready=%s\n",
                  batch->commandCount, batch->estimatedBatchTime,
                  batch->readyForExecution ? "YES" : "NO");
  }
  
  return batch->commandCount > 0;
}

bool SNet_consolidateBatch(CommandBatch* batch) {
  if (batch->commandCount <= 1) {
    // Single command - just copy payload
    if (batch->commandCount == 1) {
      strcpy(batch->consolidatedPayload, batch->commands[0].payload);
    }
    return true;
  }
  
  // Build consolidated JSON payload
  JsonDocument doc;
  
  for (uint8_t i = 0; i < batch->commandCount; i++) {
    SmartNetworkCommand* cmd = &batch->commands[i];
    
    // Parse individual command payload
    JsonDocument cmdDoc;
    DeserializationError error = deserializeJson(cmdDoc, cmd->payload);
    
    if (!error) {
      // Merge command into main document
      for (JsonPair kv : cmdDoc.as<JsonObject>()) {
        doc[kv.key()] = kv.value();
      }
    } else {
      Serial.printf("[SNET] Failed to parse command payload: %s\n", cmd->payload);
    }
  }
  
  // Serialize consolidated payload
  size_t payloadSize = serializeJson(doc, batch->consolidatedPayload, sizeof(batch->consolidatedPayload));
  
  if (payloadSize == 0) {
    Serial.println("[SNET] Failed to serialize consolidated payload");
    return false;
  }
  
  Serial.printf("[SNET] Consolidated %d commands into %d bytes\n", 
                batch->commandCount, payloadSize);
  
  return true;
}

// ───── Batch Execution ─────
bool SNet_executeBatch(CommandBatch* batch) {
  if (!batch->readyForExecution || batch->commandCount == 0) return false;
  
  uint32_t startTime = millis();
  bool success = false;
  
  // Consolidate batch payload
  if (!SNet_consolidateBatch(batch)) {
    Serial.println("[SNET] Failed to consolidate batch - executing individually");
    return SNet_executeIndividualCommands(batch);
  }
  
  // Try WebSocket first if available
  if (WebSocketClient_isConnected()) {
    success = WebSocketClient_queueMessage(batch->consolidatedPayload);
    if (success) {
      Serial.printf("[SNET] Batch sent via WebSocket: %s\n", batch->consolidatedPayload);
    }
  }
  
  // Fallback to HTTP if WebSocket unavailable or failed
  if (!success) {
    success = WLEDClient_sendCommand(batch->consolidatedPayload);
    if (success) {
      Serial.printf("[SNET] Batch sent via HTTP: %s\n", batch->consolidatedPayload);
    }
  }
  
  uint32_t executionTime = millis() - startTime;
  
  // Update performance metrics
  SNet_context.metrics.totalCommandsSent += batch->commandCount;
  SNet_context.metrics.batchesProcessed++;
  
  if (success) {
    // Update success metrics
    SNet_context.metrics.avgLatency = (SNet_context.metrics.avgLatency * 7 + executionTime) / 8;
    SNet_context.metrics.peakLatency = max(SNet_context.metrics.peakLatency, executionTime);
    
    // Notify other systems
    FreqManager_notifyNetworkActivity();
    PScale_notifyEvent(2, executionTime, batch->commandCount * 2); // PERF_NETWORK_BURST
  } else {
    SNet_context.metrics.totalCommandsFailed += batch->commandCount;
    Serial.printf("[SNET] Batch execution failed after %dms\n", executionTime);
  }
  
  // Clear batch
  batch->commandCount = 0;
  batch->readyForExecution = false;
  
  return success;
}

bool SNet_executeIndividualCommands(CommandBatch* batch) {
  bool allSucceeded = true;
  
  for (uint8_t i = 0; i < batch->commandCount; i++) {
    SmartNetworkCommand* cmd = &batch->commands[i];
    bool success = false;
    
    uint32_t startTime = millis();
    
    // Try WebSocket first if available
    if (WebSocketClient_isConnected()) {
      success = WebSocketClient_queueMessage(cmd->payload);
    }
    
    // Fallback to HTTP
    if (!success) {
      success = WLEDClient_sendCommand(cmd->payload);
    }
    
    uint32_t executionTime = millis() - startTime;
    
    if (success) {
      SNet_context.metrics.avgLatency = (SNet_context.metrics.avgLatency * 7 + executionTime) / 8;
    } else {
      allSucceeded = false;
      
      // Queue for retry if retries are enabled
      if (SNet_context.intelligentRetryEnabled && cmd->retryCount < ADAPTIVE_RETRY_MAX_COUNT) {
        SNet_queueRetry(cmd);
      }
    }
    
    Serial.printf("[SNET] Individual command %d: %s (%dms)\n", 
                  i, success ? "OK" : "FAILED", executionTime);
  }
  
  return allSucceeded;
}

// ───── Intelligent Retry Logic ─────
void SNet_queueRetry(SmartNetworkCommand* cmd) {
  if (SNet_context.queueCount >= PRIORITY_QUEUE_SIZE) return;
  
  cmd->retryCount++;
  cmd->lastRetry = millis();
  
  // Calculate adaptive retry delay
  uint32_t baseDelay = SNet_context.metrics.adaptiveRetryDelay;
  uint32_t backoffDelay = baseDelay * (1 << (cmd->retryCount - 1)); // Exponential backoff
  cmd->timestamp = millis() + min(backoffDelay, 5000UL); // Max 5s delay
  
  // Reduce priority for retries
  if (cmd->priority < PRIORITY_LOW) {
    cmd->priority = PRIORITY_LOW;
  }
  
  // Add back to queue
  SNet_context.priorityQueue[SNet_context.queueCount++] = *cmd;
  
  Serial.printf("[SNET] Queued retry %d for command type %d (delay=%dms)\n",
                cmd->retryCount, cmd->commandType, cmd->timestamp - millis());
}

// ───── Network Congestion Detection ─────
void SNet_updateCongestionMetrics() {
  uint32_t now = millis();
  
  if (now - SNet_context.lastCongestionCheck < 1000) return; // Check every second
  SNet_context.lastCongestionCheck = now;
  
  NetworkPerformanceMetrics* metrics = &SNet_context.metrics;
  
  // Calculate success rate
  uint32_t totalCommands = metrics->totalCommandsSent + metrics->totalCommandsFailed;
  if (totalCommands > 0) {
    metrics->successRate = (metrics->totalCommandsSent * 100) / totalCommands;
  }
  
  // Detect congestion based on latency and success rate
  bool highLatency = metrics->avgLatency > NETWORK_CONGESTION_THRESHOLD;
  bool lowSuccessRate = metrics->successRate < 90;
  
  metrics->networkCongested = highLatency || lowSuccessRate;
  
  if (metrics->networkCongested) {
    // Increase batching threshold and adaptive retry delay
    SNet_context.batchingThreshold = min(SNet_context.batchingThreshold + 10, 200UL);
    metrics->adaptiveRetryDelay = min(metrics->adaptiveRetryDelay + 100, 2000UL);
    SNet_context.currentBatchSize = min(SNet_context.currentBatchSize + 1, MAX_BATCH_COMMANDS);
    
    metrics->congestionLevel = min(metrics->congestionLevel + 10, 100UL);
  } else {
    // Reduce batching threshold and retry delay
    SNet_context.batchingThreshold = max(SNet_context.batchingThreshold - 5, 20UL);
    metrics->adaptiveRetryDelay = max(metrics->adaptiveRetryDelay - 50, 100UL);
    SNet_context.currentBatchSize = max((int)(SNet_context.currentBatchSize - 1), 2);
    
    metrics->congestionLevel = max((int32_t)metrics->congestionLevel - 5, 0L);
  }
}

// ───── Core Functions ─────
void SNet_init() {
  Serial.println("[SNET] Initializing smart network batching...");
  
  // Initialize context
  memset(&SNet_context, 0, sizeof(SmartNetworkContext));
  
  // Set default values
  SNet_context.adaptiveBatchingEnabled = true;
  SNet_context.intelligentRetryEnabled = true;
  SNet_context.currentBatchSize = 3; // Start with small batches
  SNet_context.batchingThreshold = BATCH_TIMEOUT_MS;
  SNet_context.metrics.adaptiveRetryDelay = 500; // 500ms initial retry delay
  SNet_context.metrics.avgLatency = 200; // Assume 200ms initial latency
  
  SNet_initialized = true;
  Serial.println("[SNET] Smart network batching initialized");
}

void SNet_update() {
  if (!SNet_initialized) return;
  
  uint32_t now = millis();
  
  // Update congestion metrics
  SNet_updateCongestionMetrics();
  
  // Remove expired commands
  SNet_removeExpiredCommands();
  
  // Deduplicate queue periodically
  if (SNet_context.queueCount > 3) {
    SNet_deduplicateQueue();
  }
  
  // Process batches
  if (now - SNet_context.lastBatchProcess >= 10) { // Process every 10ms
    SNet_context.lastBatchProcess = now;
    
    if (SNet_createBatch()) {
      if (SNet_context.currentBatch.readyForExecution) {
        SNet_executeBatch(&SNet_context.currentBatch);
      }
    }
  }
}

void SNet_removeExpiredCommands() {
  uint32_t now = millis();
  
  for (uint8_t i = 0; i < SNet_context.queueCount; i++) {
    SmartNetworkCommand* cmd = &SNet_context.priorityQueue[i];
    
    if (now > cmd->deadline) {
      Serial.printf("[SNET] Command type %d expired (age=%dms)\n", 
                    cmd->commandType, now - cmd->timestamp);
      cmd->timedOut = true;
    }
  }
  
  SNet_compactQueue();
}

// ───── Public API ─────
bool SNet_queueCommand(uint8_t commandType, uint8_t value) {
  return SNet_queueSmartCommand(commandType, value, millis() + 5000);
}

bool SNet_queuePriorityCommand(uint8_t commandType, uint8_t value, NetworkCommandPriority priority) {
  if (SNet_queueSmartCommand(commandType, value, millis() + 5000)) {
    // Override priority for last queued command
    if (SNet_context.queueCount > 0) {
      SNet_context.priorityQueue[SNet_context.queueCount - 1].priority = priority;
      SNet_sortQueueByPriority();
    }
    return true;
  }
  return false;
}

// ───── Statistics and Control ─────
void SNet_printStats() {
  NetworkPerformanceMetrics* metrics = &SNet_context.metrics;
  
  Serial.printf("[SNET] Queue: %d commands, %d batch size, threshold=%dms\n",
                SNet_context.queueCount, SNet_context.currentBatchSize, 
                SNet_context.batchingThreshold);
  Serial.printf("[SNET] Performance: %dms avg latency, %d%% success, congestion=%d%%\n",
                metrics->avgLatency, metrics->successRate, metrics->congestionLevel);
  Serial.printf("[SNET] Totals: %d sent, %d failed, %d batches, %d deduplicated\n",
                metrics->totalCommandsSent, metrics->totalCommandsFailed,
                metrics->batchesProcessed, metrics->commandsDeduplicated);
  Serial.printf("[SNET] Adaptive: retry delay=%dms, batching %s, retries %s\n",
                metrics->adaptiveRetryDelay,
                SNet_context.adaptiveBatchingEnabled ? "ON" : "OFF",
                SNet_context.intelligentRetryEnabled ? "ON" : "OFF");
}

uint32_t SNet_getQueueDepth() {
  return SNet_context.queueCount;
}

float SNet_getSuccessRate() {
  return SNet_context.metrics.successRate / 100.0f;
}

bool SNet_isNetworkCongested() {
  return SNet_context.metrics.networkCongested;
}

uint32_t SNet_getAvgLatency() {
  return SNet_context.metrics.avgLatency;
}

#else // !ENABLE_SMART_CACHING (network optimizations disabled)

// ───── Stub Functions When Smart Network is Disabled ─────
void SNet_init() {}
void SNet_update() {}
bool SNet_queueCommand(uint8_t commandType, uint8_t value) { return NetworkTask_queueCommand(commandType, value); }
bool SNet_queuePriorityCommand(uint8_t commandType, uint8_t value, uint8_t priority) { return NetworkTask_queueCommand(commandType, value); }
void SNet_printStats() { Serial.println("[SNET] Smart network batching disabled"); }
uint32_t SNet_getQueueDepth() { return 0; }
float SNet_getSuccessRate() { return 1.0f; }
bool SNet_isNetworkCongested() { return false; }
uint32_t SNet_getAvgLatency() { return 0; }

#endif // ENABLE_SMART_CACHING