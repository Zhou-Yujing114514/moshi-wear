/**
 * 摩柿小说 —— 会话管理（登录令牌的内存态 + 持久化）
 * @system.storage 仅支持字符串且为异步回调，故令牌先放内存，再异步落盘。
 */
import storage from '@system.storage'
import config from './config.js'

// 内存态，避免每次请求都走异步存储
let memoryToken = ''
let memoryUsername = ''

export function getToken() {
  return memoryToken
}

export function getUsername() {
  return memoryUsername
}

/** 写入会话：更新内存并持久化 */
export function saveSession(token, username) {
  memoryToken = token || ''
  memoryUsername = username || ''
  storage.set({
    key: config.storageKeys.token,
    value: memoryToken
  })
  if (memoryUsername) {
    storage.set({
      key: config.storageKeys.username,
      value: memoryUsername
    })
  }
  return memoryToken
}

/** 清除会话（退出登录） */
export function clearSession() {
  memoryToken = ''
  memoryUsername = ''
  storage.delete({ key: config.storageKeys.token })
  storage.delete({ key: config.storageKeys.username })
}

/** 应用启动时从持久层恢复会话，返回 Promise<{token, username}> */
export function restoreSession() {
  return new Promise((resolve) => {
    storage.get({
      key: config.storageKeys.token,
      success: (token) => {
        memoryToken = token || ''
        storage.get({
          key: config.storageKeys.username,
          success: (name) => {
            memoryUsername = name || ''
            resolve({ token: memoryToken, username: memoryUsername })
          },
          fail: () => resolve({ token: memoryToken, username: '' })
        })
      },
      fail: () => resolve({ token: '', username: '' })
    })
  })
}

export function isLoggedIn() {
  return !!memoryToken
}
