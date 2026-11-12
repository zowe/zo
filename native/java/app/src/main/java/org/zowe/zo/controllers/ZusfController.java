package org.zowe.zo.controllers;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.*;
import org.zowe.zo.ffm.ZusfService;

import java.util.Map;

@RestController
@RequestMapping("/zosmf/restfiles/fs")
@RequiredArgsConstructor
@Slf4j
public class ZusfController {

    private final ZusfService zusfService;

    @GetMapping(produces = "text/plain;charset=UTF-8")
    public String listUssDir(@RequestParam("path") String path,
                             @RequestParam(value = "all", defaultValue = "false") boolean allFiles,
                             @RequestParam(value = "long", defaultValue = "false") boolean longFormat) throws Exception {
        if (path == null || path.isEmpty()) {
            throw new IllegalArgumentException("path parameter is required");
        }
        // Returns the CSV or string formatted by the native C++ code
        return zusfService.listUssDir(path, allFiles, longFormat, 1);
    }

    @GetMapping(value = "/**", produces = "text/plain;charset=UTF-8")
    public String readUssFile(@RequestParam("path") String path,
                              @RequestParam(value = "encoding", required = false) String encoding) throws Exception {
        return zusfService.readUssFile(path, encoding != null ? encoding : "");
    }

    @PutMapping(value = "/**", produces = "text/plain;charset=UTF-8")
    public String writeUssFile(@RequestParam("path") String path,
                               @RequestBody String data,
                               @RequestParam(value = "encoding", required = false) String encoding) throws Exception {
        return zusfService.writeUssFile(path, data, encoding != null ? encoding : "", "");
    }

    @PostMapping("/file")
    public Map<String, String> createUssFile(@RequestParam("path") String path,
                                             @RequestParam(value = "mode", defaultValue = "644") String mode) throws Exception {
        zusfService.createUssFile(path, mode);
        return Map.of("message", "File created successfully");
    }

    @PostMapping("/dir")
    public Map<String, String> createUssDir(@RequestParam("path") String path,
                                            @RequestParam(value = "mode", defaultValue = "755") String mode) throws Exception {
        zusfService.createUssDir(path, mode);
        return Map.of("message", "Directory created successfully");
    }

    @PutMapping("/move")
    public Map<String, String> moveUssFileOrDir(@RequestParam("source") String source,
                                                @RequestParam("destination") String destination) throws Exception {
        zusfService.moveUssFileOrDir(source, destination);
        return Map.of("message", "Moved successfully");
    }

    @PutMapping("/chmod")
    public Map<String, String> chmodUssItem(@RequestParam("path") String path,
                                            @RequestParam("mode") String mode,
                                            @RequestParam(value = "recursive", defaultValue = "false") boolean recursive) throws Exception {
        zusfService.chmodUssItem(path, mode, recursive);
        return Map.of("message", "Permissions changed successfully");
    }

    @PutMapping("/chown")
    public Map<String, String> chownUssItem(@RequestParam("path") String path,
                                            @RequestParam("owner") String owner,
                                            @RequestParam(value = "recursive", defaultValue = "false") boolean recursive) throws Exception {
        zusfService.chownUssItem(path, owner, recursive);
        return Map.of("message", "Owner changed successfully");
    }

    @PutMapping("/chtag")
    public Map<String, String> chtagUssItem(@RequestParam("path") String path,
                                            @RequestParam("tag") String tag,
                                            @RequestParam(value = "recursive", defaultValue = "false") boolean recursive) throws Exception {
        zusfService.chtagUssItem(path, tag, recursive);
        return Map.of("message", "Tag changed successfully");
    }

    @DeleteMapping("/**")
    public Map<String, String> deleteUssItem(@RequestParam("path") String path,
                                             @RequestParam(value = "recursive", defaultValue = "false") boolean recursive) throws Exception {
        zusfService.deleteUssItem(path, recursive);
        return Map.of("message", "Deleted successfully");
    }
}
