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
    setExistingProfile?: boolean;
    prioritizeProjectLevelConfig?: boolean;
    disableCreateNewProfile?: boolean;
    /**
     * Keep the connection that validated the selected profile open instead of disposing it, so the caller can
     * reuse it (see `AbstractConfigManager.takeRetainedConnection`) and the user authenticates, e.g. with MFA, once.
     */
    retainConnection?: boolean;
    /**
     * Seconds between keep-alive messages on the retained connection (default: 30). Keep-alive can only be set when
     * connecting, so this must be known up front for a connection that will later host the long-lived server.
     */
    keepAliveInterval?: number;
}
