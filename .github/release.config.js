module.exports = {
    branches: [
        {
            name: "main",
            level: "minor",
        }
    ],
    plugins: [
        [
            "@octorelease/changelog",
            {
                displayNames: {
                    "cli": "CLI Plug-in",
                    "sdk": "Client SDK",
                    "native": "z/OS Components"
                },
                extraDirs: ["native"],
                headerLine: "## Recent Changes",
            }
        ],
        [
            "@octorelease/lerna",
            {
                // Use Lerna only for versioning and publish packages independently
                npmPublish: false,
            },
        ],
        [
            "@octorelease/exec",
            {
                $cwd: "packages/cli",
                dryRunAllow: ["publish"],
                publishCmd: "npm run package",
            },
        ],
        [
            "@octorelease/npm",
            {
                $cwd: "packages/sdk",
                npmPublish: true,
                tarballDir: "dist",
            },
        ],
        [
            "@octorelease/github",
            {
                assets: ["dist/*.pax.Z", "dist/*.tgz"]
            },
        ],
        "@octorelease/git",
    ]
};
