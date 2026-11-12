package org.zowe.zo.controllers;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZusfService;

import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@ExtendWith(MockitoExtension.class)
class ZusfControllerTest {

    @Mock
    ZusfService zusfService;

    MockMvc mockMvc;

    private static final String BASE = "/zosmf/restfiles/fs";

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZusfController(zusfService))
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    @Test
    void listUssDir_returnsDirectoryListing() throws Exception {
        when(zusfService.listUssDir("/u/user", false, false, 1)).thenReturn("file1.txt\nfile2.txt");

        mockMvc.perform(get(BASE).param("path", "/u/user"))
               .andExpect(status().isOk())
               .andExpect(content().string("file1.txt\nfile2.txt"));
    }

    @Test
    void listUssDir_withAllAndLongFlags_passesToService() throws Exception {
        when(zusfService.listUssDir("/u/user", true, true, 1)).thenReturn("listing");

        mockMvc.perform(get(BASE)
                .param("path", "/u/user")
                .param("all", "true")
                .param("long", "true"))
               .andExpect(status().isOk());

        verify(zusfService).listUssDir("/u/user", true, true, 1);
    }

    @Test
    void listUssDir_defaultFlags_areFalse() throws Exception {
        when(zusfService.listUssDir(any(), eq(false), eq(false), eq(1))).thenReturn("listing");

        mockMvc.perform(get(BASE).param("path", "/u/user"))
               .andExpect(status().isOk());

        verify(zusfService).listUssDir("/u/user", false, false, 1);
    }

    @Test
    void listUssDir_emptyPath_returns400() throws Exception {
        mockMvc.perform(get(BASE).param("path", ""))
               .andExpect(status().isBadRequest())
               .andExpect(jsonPath("$.message").value("path parameter is required"));
    }

    @Test
    void listUssDir_serviceThrowsException_returns500() throws Exception {
        when(zusfService.listUssDir(any(), anyBoolean(), anyBoolean(), anyInt()))
                .thenThrow(new RuntimeException("Directory error"));

        mockMvc.perform(get(BASE).param("path", "/u/user"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("Directory error"));
    }

    @Test
    void readUssFile_returnsFileContent() throws Exception {
        when(zusfService.readUssFile("/u/user/file.txt", "")).thenReturn("file content here");

        mockMvc.perform(get(BASE + "/u/user/file.txt").param("path", "/u/user/file.txt"))
               .andExpect(status().isOk())
               .andExpect(content().string("file content here"));
    }

    @Test
    void readUssFile_withEncoding_passesEncoding() throws Exception {
        when(zusfService.readUssFile("/u/user/file.txt", "ISO-8859-1")).thenReturn("content");

        mockMvc.perform(get(BASE + "/u/user/file.txt")
                .param("path", "/u/user/file.txt")
                .param("encoding", "ISO-8859-1"))
               .andExpect(status().isOk());

        verify(zusfService).readUssFile("/u/user/file.txt", "ISO-8859-1");
    }

    @Test
    void readUssFile_withoutEncoding_usesEmptyString() throws Exception {
        when(zusfService.readUssFile(any(), eq(""))).thenReturn("data");

        mockMvc.perform(get(BASE + "/u/user/file.txt").param("path", "/u/user/file.txt"))
               .andExpect(status().isOk());

        verify(zusfService).readUssFile("/u/user/file.txt", "");
    }

    @Test
    void writeUssFile_returnsEtag() throws Exception {
        when(zusfService.writeUssFile("/u/user/file.txt", "new data", "", "")).thenReturn("etag-xyz");

        mockMvc.perform(put(BASE + "/u/user/file.txt")
                .param("path", "/u/user/file.txt")
                .contentType(MediaType.TEXT_PLAIN)
                .content("new data"))
               .andExpect(status().isOk())
               .andExpect(content().string("etag-xyz"));
    }

    @Test
    void writeUssFile_withEncoding_passesEncoding() throws Exception {
        when(zusfService.writeUssFile(any(), any(), eq("IBM-1047"), any())).thenReturn("");

        mockMvc.perform(put(BASE + "/u/user/file.txt")
                .param("path", "/u/user/file.txt")
                .param("encoding", "IBM-1047")
                .contentType(MediaType.TEXT_PLAIN)
                .content("data"))
               .andExpect(status().isOk());

        verify(zusfService).writeUssFile("/u/user/file.txt", "data", "IBM-1047", "");
    }

    @Test
    void createUssFile_withDefaultMode_returnsSuccess() throws Exception {
        mockMvc.perform(post(BASE + "/file")
                .param("path", "/u/user/newfile.txt"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("File created successfully"));

        verify(zusfService).createUssFile("/u/user/newfile.txt", "644");
    }

    @Test
    void createUssFile_withCustomMode_passesMode() throws Exception {
        mockMvc.perform(post(BASE + "/file")
                .param("path", "/u/user/newfile.txt")
                .param("mode", "755"))
               .andExpect(status().isOk());

        verify(zusfService).createUssFile("/u/user/newfile.txt", "755");
    }

    @Test
    void createUssDir_withDefaultMode_returnsSuccess() throws Exception {
        mockMvc.perform(post(BASE + "/dir")
                .param("path", "/u/user/newdir"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Directory created successfully"));

        verify(zusfService).createUssDir("/u/user/newdir", "755");
    }

    @Test
    void createUssDir_withCustomMode_passesMode() throws Exception {
        mockMvc.perform(post(BASE + "/dir")
                .param("path", "/u/user/newdir")
                .param("mode", "700"))
               .andExpect(status().isOk());

        verify(zusfService).createUssDir("/u/user/newdir", "700");
    }

    @Test
    void moveUssFileOrDir_returnsSuccessMessage() throws Exception {
        mockMvc.perform(put(BASE + "/move")
                .param("source", "/u/user/old.txt")
                .param("destination", "/u/user/new.txt"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Moved successfully"));

        verify(zusfService).moveUssFileOrDir("/u/user/old.txt", "/u/user/new.txt");
    }

    @Test
    void chmodUssItem_withDefaultRecursive_returnSuccess() throws Exception {
        mockMvc.perform(put(BASE + "/chmod")
                .param("path", "/u/user/file.txt")
                .param("mode", "755"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Permissions changed successfully"));

        verify(zusfService).chmodUssItem("/u/user/file.txt", "755", false);
    }

    @Test
    void chmodUssItem_recursive_passesFlag() throws Exception {
        mockMvc.perform(put(BASE + "/chmod")
                .param("path", "/u/user/dir")
                .param("mode", "755")
                .param("recursive", "true"))
               .andExpect(status().isOk());

        verify(zusfService).chmodUssItem("/u/user/dir", "755", true);
    }

    @Test
    void chownUssItem_returnsSuccessMessage() throws Exception {
        mockMvc.perform(put(BASE + "/chown")
                .param("path", "/u/user/file.txt")
                .param("owner", "newuser"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Owner changed successfully"));

        verify(zusfService).chownUssItem("/u/user/file.txt", "newuser", false);
    }

    @Test
    void chownUssItem_recursive_passesFlag() throws Exception {
        mockMvc.perform(put(BASE + "/chown")
                .param("path", "/u/user/dir")
                .param("owner", "newuser")
                .param("recursive", "true"))
               .andExpect(status().isOk());

        verify(zusfService).chownUssItem("/u/user/dir", "newuser", true);
    }

    @Test
    void chtagUssItem_returnsSuccessMessage() throws Exception {
        mockMvc.perform(put(BASE + "/chtag")
                .param("path", "/u/user/file.txt")
                .param("tag", "IBM-1047"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Tag changed successfully"));

        verify(zusfService).chtagUssItem("/u/user/file.txt", "IBM-1047", false);
    }

    @Test
    void chtagUssItem_recursive_passesFlag() throws Exception {
        mockMvc.perform(put(BASE + "/chtag")
                .param("path", "/u/user/dir")
                .param("tag", "IBM-1047")
                .param("recursive", "true"))
               .andExpect(status().isOk());

        verify(zusfService).chtagUssItem("/u/user/dir", "IBM-1047", true);
    }

    @Test
    void deleteUssItem_returnsSuccessMessage() throws Exception {
        mockMvc.perform(delete(BASE + "/u/user/file.txt")
                .param("path", "/u/user/file.txt"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Deleted successfully"));

        verify(zusfService).deleteUssItem("/u/user/file.txt", false);
    }

    @Test
    void deleteUssItem_recursive_passesFlag() throws Exception {
        mockMvc.perform(delete(BASE + "/u/user/dir")
                .param("path", "/u/user/dir")
                .param("recursive", "true"))
               .andExpect(status().isOk());

        verify(zusfService).deleteUssItem("/u/user/dir", true);
    }

    @Test
    void deleteUssItem_serviceThrowsException_returns500() throws Exception {
        doThrow(new RuntimeException("Delete failed")).when(zusfService).deleteUssItem(any(), anyBoolean());

        mockMvc.perform(delete(BASE + "/u/user/file.txt")
                .param("path", "/u/user/file.txt"))
               .andExpect(status().isInternalServerError());
    }
}
