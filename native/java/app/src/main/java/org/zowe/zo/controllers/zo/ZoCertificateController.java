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

package org.zowe.zo.controllers.zo;

import com.fasterxml.jackson.databind.JsonNode;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;
import lombok.RequiredArgsConstructor;
import org.springframework.http.CacheControl;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.CertificateRequests.CertificateQuery;
import org.zowe.zo.service.CertificateRequests.ExportRequest;
import org.zowe.zo.service.CertificateRequests.ImportRequest;
import org.zowe.zo.service.CertificateRequests.OwnerCertificateAction;
import org.zowe.zo.service.CertificateRequests.RingCertificateAction;
import org.zowe.zo.service.CertificateService;
import org.zowe.zo.service.ZoCertificateException;

@RestController
@RequestMapping("/zo/api/v1")
@RequiredArgsConstructor
@Tag(name = "zo Certificates", description = "RACF key rings and certificates (zo extension, not available in z/OSMF). "
        + "Passwords are accepted in request bodies only and are never logged.")
public class ZoCertificateController {

    private static final String RING = "/keyrings/{owner}/{ring}";
    private static final String RING_CERT = RING + "/certificates/{label}";
    private static final String OWNER_CERT = "/certificates/{owner}/{label}";

    private final CertificateService certificateService;

    @GetMapping("/keyrings/{owner}")
    @Operation(summary = "List a user's key rings with their certificates")
    public JsonNode listRings(@PathVariable String owner) throws ZoRpcException, ZoCertificateException {
        return certificateService.listRings(owner, null);
    }

    @GetMapping(RING)
    @Operation(summary = "Show one key ring and its certificates")
    public JsonNode getRing(@PathVariable String owner, @PathVariable String ring)
            throws ZoRpcException, ZoCertificateException {
        return certificateService.listRings(owner, ring);
    }

    @GetMapping(RING + "/count")
    @Operation(summary = "Count the certificates on a key ring ('*' for the owner's virtual ring)")
    public JsonNode countRing(@PathVariable String owner, @PathVariable String ring)
            throws ZoRpcException, ZoCertificateException {
        return certificateService.countRing(owner, ring);
    }

    @PostMapping(RING)
    @Operation(summary = "Create a key ring")
    public ResponseEntity<JsonNode> createKeyring(@PathVariable String owner, @PathVariable String ring)
            throws ZoRpcException, ZoCertificateException {
        return ResponseEntity.status(HttpStatus.CREATED).body(certificateService.createKeyring(owner, ring));
    }

    @DeleteMapping(RING)
    @Operation(summary = "Delete a key ring")
    public ResponseEntity<Void> deleteKeyring(@PathVariable String owner, @PathVariable String ring)
            throws ZoRpcException, ZoCertificateException {
        certificateService.deleteKeyring(owner, ring);
        return ResponseEntity.noContent().build();
    }

    @PostMapping("/keyrings/refresh")
    @Operation(summary = "Refresh the DIGTCERT class")
    public JsonNode refreshDigtcert() throws ZoRpcException, ZoCertificateException {
        return certificateService.refreshDigtcert();
    }

    @GetMapping(RING + "/certificates")
    @Operation(summary = "List certificates on a key ring",
            description = "maxEntries defaults to 10 natively; 0 returns all. Filters are exact and case-sensitive.")
    public JsonNode listCertificates(@PathVariable String owner, @PathVariable String ring,
                                     @RequestParam(required = false) String label,
                                     @RequestParam(required = false) String usage,
                                     @Parameter(description = "Return labels only")
                                     @RequestParam(required = false) Boolean labelOnly,
                                     @RequestParam(required = false) Boolean ownerOnly,
                                     @RequestParam(required = false) Integer maxEntries)
            throws ZoRpcException, ZoCertificateException {
        return certificateService.listCertificates(owner, ring,
                new CertificateQuery(label, usage, labelOnly, ownerOnly, maxEntries));
    }

    @GetMapping(RING_CERT)
    @Operation(summary = "Show certificate details")
    public JsonNode showCertificate(@PathVariable String owner, @PathVariable String ring, @PathVariable String label)
            throws ZoRpcException, ZoCertificateException {
        return certificateService.showCertificate(owner, ring, label);
    }

    @GetMapping(RING_CERT + "/export")
    @Operation(summary = "Export a certificate as PEM",
            description = "Only PEM can be exported by GET; use POST for p12 so the passphrase stays in the body.")
    public ResponseEntity<JsonNode> exportPem(@PathVariable String owner, @PathVariable String ring,
                                              @PathVariable String label,
                                              @RequestParam(required = false) String format,
                                              @RequestParam(required = false) String file,
                                              @RequestParam(required = false) String dsn)
            throws ZoRpcException, ZoCertificateException {
        if (format != null && !"pem".equalsIgnoreCase(format)) {
            throw new IllegalArgumentException("GET export supports pem only; use POST for p12");
        }
        return noStore(certificateService.exportCertificate(owner, ring, label,
                new ExportRequest("pem", file, dsn, null)));
    }

    @PostMapping(RING_CERT + "/export")
    @Operation(summary = "Export a certificate (pem or p12)",
            description = "p12 requires a passphrase (8-128 characters) and a file or dsn. "
                    + "The response contains base64 certificate or key material and is not cacheable.")
    public ResponseEntity<JsonNode> exportCertificate(@PathVariable String owner, @PathVariable String ring,
                                                      @PathVariable String label, @RequestBody ExportRequest request)
            throws ZoRpcException, ZoCertificateException {
        return noStore(certificateService.exportCertificate(owner, ring, label, request));
    }

    @PostMapping("/certificates/import")
    @Operation(summary = "Import a PKCS#12 bundle from a USS file or data set into a key ring")
    public ResponseEntity<JsonNode> importCertificate(@RequestBody ImportRequest request)
            throws ZoRpcException, ZoCertificateException {
        return ResponseEntity.status(HttpStatus.CREATED).body(certificateService.importCertificate(request));
    }

    @PutMapping(RING_CERT)
    @Operation(summary = "Connect a certificate to a ring or set it as the ring default",
            description = "Body request: \"connect\" (fromRing xor fromDatabase, optional usage and default) or \"set-default\".")
    public JsonNode ringAction(@PathVariable String owner, @PathVariable String ring, @PathVariable String label,
                               @RequestBody RingCertificateAction action)
            throws ZoRpcException, ZoCertificateException {
        return certificateService.ringAction(owner, ring, label, action);
    }

    @PutMapping(OWNER_CERT)
    @Operation(summary = "Rename a certificate or change its trust status",
            description = "Body request: \"rename\" (newLabel) or \"trust\" (status TRUST, HIGHTRUST or NOTRUST).")
    public JsonNode ownerAction(@PathVariable String owner, @PathVariable String label,
                                @RequestBody OwnerCertificateAction action)
            throws ZoRpcException, ZoCertificateException {
        return certificateService.ownerAction(owner, label, action);
    }

    @DeleteMapping(RING_CERT)
    @Operation(summary = "Disconnect a certificate from a key ring")
    public ResponseEntity<Void> disconnectCertificate(@PathVariable String owner, @PathVariable String ring,
                                                      @PathVariable String label,
                                                      @RequestParam(required = false) Boolean skipRefresh)
            throws ZoRpcException, ZoCertificateException {
        certificateService.deleteCertificate(owner, ring, label, skipRefresh);
        return ResponseEntity.noContent().build();
    }

    @DeleteMapping(OWNER_CERT)
    @Operation(summary = "Delete a certificate from the ESM database (removes it from every ring)")
    public ResponseEntity<Void> deleteCertificate(@PathVariable String owner, @PathVariable String label,
                                                  @RequestParam(required = false) Boolean skipRefresh)
            throws ZoRpcException, ZoCertificateException {
        certificateService.deleteCertificate(owner, null, label, skipRefresh);
        return ResponseEntity.noContent().build();
    }

    private static ResponseEntity<JsonNode> noStore(JsonNode body) {
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(body);
    }
}
