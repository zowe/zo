/**
 * This program and the accompanying materials are made available under the terms of the
 * Eclipse Public License v2.0 which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v20.html
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * Copyright Contributors to the Zowe Project.
 *
 */

export interface inputBoxOpts {
    title: string;
    placeHolder?: string;
    password?: boolean;
    value?: string;
    validateInput?: (input: string) => string;
}
export interface qpOpts {
    items: qpItem[];
    title?: string;
    placeholder?: string;
    value?: string;
}
export interface qpItem {
    label: string;
    description?: string;
    separator?: boolean;
}
export enum MESSAGE_TYPE {
    INFORMATION = 1,
    WARNING = 2,
    ERROR = 3,
}

export interface PrivateKeyWarningOptions {
    profileName: string;
    privateKeyPath: string;
    onUndo?: () => void | Promise<void>;
    onDelete?: () => void | Promise<void>;
}

// Interface for vscode Disposable when leveraging in the SDK
export interface IDisposable {
    dispose(): void;
}

export interface PromptForProfileOptions {
    /** Seconds between keep-alive messages when validation creates the cached transport (default: 30). */
    keepAliveInterval?: number;

    setExistingProfile?: boolean;
    prioritizeProjectLevelConfig?: boolean;
    disableCreateNewProfile?: boolean;
}
