// SPDX-License-Identifier: GPL-2.0-or-later
export class LobbyError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.name = 'LobbyError';
    this.code = code;
  }
}
